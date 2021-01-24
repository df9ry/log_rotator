#include "version.hpp"
#include "getopt.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <regex>
#include <thread>
#include <ctime>
#include <chrono>
#include <ratio>
#include <filesystem>
#include <cassert>

using namespace std;
using namespace chrono;

static int verbose{0};
static int hour_rotate{0};
static int min_rotate{0};
static int sec_rotate{0};
static auto next_rotation{system_clock::now()};
static string output_directory_name{"./dump"};
static string output_file_name_pattern{"%TS%.td"};
static ofstream output_file_stream{};
static string output_file_path{""};

static void print_version(ostream &os)
{
    os << "This is " << APP_NAME << " version " << APP_VERSION
       << ", Copyright (C) by " << APP_COPYRIGHT
       << " - see: " << APP_WEBSITE
       << endl;
}

static void help(ostream &os, const char *name)
{
    print_version(os);
    os << "Usage: " << name << endl
       << "\t-d <dirname> ................ Output directory" << endl
       << "\t-f <filename pattern> ....... Filename with %TS% placeholder" << endl
       << "\t-h .......................... Print help (this message)" << endl
       << "\t-t [hh[:mm[:ss]]] ........... Rotate at timepoint" << endl
       << "\t-v .......................... Verbose" << endl;
}

static regex reg_timepoint { R"(^(\d{2})(:(\d{2}))?(:(\d{2}))?$)" };

static void calc_next_rotation()
{
    auto now{chrono::system_clock::now()};
    const time_t tt{chrono::system_clock::to_time_t(now)};
    struct tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &tt);
#else
    localtime_r(&tt, &tm_now);
#endif
    struct tm tm_rotate{tm_now};
    tm_rotate.tm_hour = hour_rotate;
    tm_rotate.tm_min  = min_rotate;
    tm_rotate.tm_sec  = sec_rotate;
    std::time_t _rotate{mktime(&tm_rotate)};
    auto rotate{chrono::system_clock::from_time_t(_rotate)};
    if (!((hour_rotate >  tm_now.tm_hour) ||
         ((hour_rotate == tm_now.tm_hour) && (min_rotate >  tm_now.tm_min)) ||
         ((hour_rotate == tm_now.tm_hour) && (min_rotate == tm_now.tm_min) && (sec_rotate > tm_now.tm_sec))))
    {
        typedef std::chrono::duration<int,std::ratio<60*60*24>> days_t;
        rotate += days_t(1);
    }
    next_rotation = rotate;
    if (verbose) {
        const time_t tt_rotate{chrono::system_clock::to_time_t(rotate)};
#ifdef _WIN32
        localtime_s(&tm_rotate, &tt_rotate);
#else
        localtime_r(&tt_rotate, &tm_rotate);
#endif
        cerr << "Next rotation at "
             << dec << setw(4) << setfill('0') << tm_rotate.tm_year + 1900 << "-"
             << dec << setw(2) << setfill('0') << tm_rotate.tm_mon + 1 << "-"
             << dec << setw(2) << setfill('0') << tm_rotate.tm_mday << " "
             << dec << setw(2) << setfill('0') << tm_rotate.tm_hour << ":"
             << dec << setw(2) << setfill('0') << tm_rotate.tm_min << ":"
             << dec << setw(2) << setfill('0') << tm_rotate.tm_sec << endl;
    }
}

static void rotate_at(const string &timepoint)
{
    smatch sm;
    if (!regex_match(timepoint, sm, reg_timepoint))
        throw runtime_error("Invalid timepoint string \"" + timepoint + "\"!");
    hour_rotate = stoi(sm[1].str());
    string _min{sm[3].str()};
    string _sec(sm[5].str());
    min_rotate = _min.empty() ? 0 : stoi(_min);
    sec_rotate = _sec.empty() ? 0 : stoi(_sec);
    calc_next_rotation();
}

static void new_output_file_stream()
{
    auto now{chrono::system_clock::now()};
    const time_t tt{chrono::system_clock::to_time_t(now)};
    struct tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &tt);
#else
    localtime_r(&tt, &tm_now);
#endif
    ostringstream oss;
    oss << dec << setw(4) << setfill('0') << tm_now.tm_year + 1900 << "-"
        << dec << setw(2) << setfill('0') << tm_now.tm_mon + 1 << "-"
        << dec << setw(2) << setfill('0') << tm_now.tm_mday << "_"
        << dec << setw(2) << setfill('0') << tm_now.tm_hour << "-"
        << dec << setw(2) << setfill('0') << tm_now.tm_min << "-"
        << dec << setw(2) << setfill('0') << tm_now.tm_sec;
    string t{oss.str()};
    size_t p{output_file_name_pattern.find("%TS%")};
    if (p == string::npos)
        throw runtime_error("Missing placeholder \"%TS% in file name pattern");
    string output_file_name{output_file_name_pattern};
    output_file_name.replace(p, 4, t);
    output_file_path = output_directory_name + "/" + output_file_name;
    if (output_file_stream.is_open())
        output_file_stream.close();
    if (verbose)
        cerr << "Open new output file \"" << output_file_path << "\"" << endl;
    output_file_stream.open(output_file_path + ".part", ios::out | ios::app);
    if (!output_file_stream.is_open())
        throw runtime_error("Unable to open output file \"" + output_file_path + ".part\"");
    if (output_file_stream.bad())
        throw runtime_error("Output file \"" + output_file_path + ".part\" is bad");
}

void close_output_file_stream()
{
    if (!output_file_stream.is_open())
        return;
    if (verbose)
        cerr << "Closing \"" << output_file_path << endl;
    output_file_stream.flush();
    output_file_stream.close();
    filesystem::rename(output_file_path + ".part", output_file_path);
}

int main(int argc, char *argv[])
{
    int option{0};
    bool good{true};

    // Get options:
    while ((option = getopt(argc, argv, "d:f:ht:v")) >= 0) {
        try {
            switch (option) {
            case 'd':
                output_directory_name = optarg;
                if (filesystem::exists(output_directory_name)) {
                    if (!filesystem::is_directory(output_directory_name))
                        throw runtime_error("File \"" + output_directory_name + "\" exists but is not a directory");
                } else {
                    if (!filesystem::create_directories(output_directory_name))
                        throw runtime_error("Unable to create directory \"" + output_directory_name + "\"");
                }
                break;
            case 'f':
                output_file_name_pattern = optarg;
                break;
            case 'h':
                help(cout, argv[0]);
                break;
            case 't':
                rotate_at(optarg);
                break;
            case 'v':
                ++verbose;
                break;
            default:
                help(cerr, argv[0]);
                throw runtime_error("Invalid option " + to_string(option));
            } // end switch //
        }
        catch (exception &ex) {
            cerr << ex.what() << "!" << endl;
            help(cerr, argv[0]);
            good = false;
        }
    } // end while //
    if (!good)
        return EXIT_FAILURE;

    try {
        if (output_directory_name.empty())
            throw runtime_error("Parameter -d missing");
        if (output_file_name_pattern.empty())
            throw runtime_error("Parameter -f missing");
        if (verbose)
            print_version(cerr);
        calc_next_rotation();
        new_output_file_stream();
    }
    catch (exception &ex) {
        cerr << ex.what() << "!" << endl;
        help(cerr, argv[0]);
        return EXIT_FAILURE;
    }

    while (good) {
        try {
            string input_line;
            getline(cin, input_line);
            if (cin.eof())
                break;
            if (cin.fail() || cin.bad()) {
                good = false;
                throw runtime_error("Error reading input file");
            }
            auto now{chrono::system_clock::now()};
            if (now > next_rotation) {
                close_output_file_stream();
                calc_next_rotation();
                new_output_file_stream();
            }
            output_file_stream << input_line << endl;
        }
        catch (exception &ex) {
            cerr << ex.what() << "!" << endl;
        }
    } // end while //
    try {
        close_output_file_stream();
    }
    catch (exception &ex) {
        cerr << ex.what() << "!" << endl;
        return EXIT_FAILURE;
    }
    catch (...) {
        cerr << "Unidentified failure" << endl;
        return EXIT_FAILURE;
    }
    return good ? EXIT_SUCCESS : EXIT_FAILURE;
}
