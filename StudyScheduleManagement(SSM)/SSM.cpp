#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>

using namespace std;

typedef struct studentAccount
{
    string Sid;
    string password;
} StudentAccount;

typedef struct studentCourse
{
    string Sid;
    string Cid;
} StudentCourse;

typedef struct course
{
    string Cid;
    string Ccode;
    string Cname;
    string CtimeRaw;    // toàn bộ chuỗi thời gian như "523,526,22,25-31,33-40,TC-502;"
    string CroomRaw;

    int dayInt = 0;        // số ngày theo mã (2=Thứ Hai, 3=Thứ Ba, ..., 7=Thứ Bảy, 1=Chủ Nhật)
    int ampm = 0;          // 1 = AM, 2 = PM
    int startPeriod = 0;   // tiết bắt đầu
    int endPeriod = 0;     // tiết kết thúc
    vector<string> weeks;  // ["22","25-31","33-40"]
    bool parsed = false;
} Course;

string currentSid;
vector<StudentAccount> studentAccounts;
vector<StudentCourse> studentCourses;
vector<Course> courses;

// helper trim
string trim(const string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

string dayNumberToName(int d) {
    switch (d) {
        case 2: return "Monday";
        case 3: return "Tuesday";
        case 4: return "Wednesday";
        case 5: return "Thursday";
        case 6: return "Friday";
        case 7: return "Saturday";
        case 1: return "Sunday";
        default: return "Day " + to_string(d);
    }
}

string ampmName(int a) {
    return a == 1 ? "Morning" : "Afternoon";
}

vector<string> splitComma(const string &s) {
    vector<string> out;
    string cur;
    stringstream ss(s);
    while (getline(ss, cur, ',')) {
        out.push_back(trim(cur));
    }
    return out;
}

static string stripNonDigits(const string &s) {
    size_t i = 0;
    while (i < s.size() && !isdigit((unsigned char)s[i])) ++i;
    size_t j = s.size();
    while (j > i && !isdigit((unsigned char)s[j - 1])) --j;
    if (i >= j) return "";
    return s.substr(i, j - i);
}

// Parse raw time string into course parsed fields
void parseCourseTime(Course &c) {
    string s = c.CtimeRaw;
    if (s.empty()) { c.parsed = false; return; }
    while (!s.empty() && (s.back() == ';' || isspace((unsigned char)s.back()))) s.pop_back();
    vector<string> tokens = splitComma(s);
    if (tokens.size() < 2) {
        c.parsed = false;
        return;
    }

    string startCode = stripNonDigits(tokens[0]);
    string endCode   = stripNonDigits(tokens[1]);

    if (startCode.size() >= 3) {
        c.dayInt = startCode[0] - '0';
        c.ampm = startCode[1] - '0';
        string sp = startCode.substr(2);
        try { c.startPeriod = stoi(sp); } catch (...) { c.startPeriod = 0; }
    }
    if (endCode.size() >= 3) {
        string ep = endCode.substr(2);
        try { c.endPeriod = stoi(ep); } catch (...) { c.endPeriod = 0; }
    }

    c.weeks.clear();
    c.CroomRaw.clear();
    for (size_t i = 2; i < tokens.size(); ++i) {
        string t = tokens[i];
        if (!t.empty() && t.back() == ';') t.pop_back();
        bool hasAlpha = false;
        for (char ch : t) if (isalpha((unsigned char)ch)) { hasAlpha = true; break; }
        if (hasAlpha || (t.find('-') != string::npos && t.find_first_not_of("0123456789-") != string::npos)) {
            c.CroomRaw = trim(t);
        } else {
            c.weeks.push_back(trim(t));
        }
    }
    c.parsed = true;
}

// try to parse a line into Course even if the file isn't tab-separated strictly
Course parseCourseLineFlexible(const string &line) {
    Course course;
    string l = trim(line);
    if (l.empty()) return course;

    // get Cid and Ccode
    stringstream ss(l);
    string cid, ccode;
    if (!(ss >> cid)) return course;
    if (!(ss >> ccode)) return course;
    course.Cid = cid;
    course.Ccode = ccode;

    size_t pos = l.find(ccode);
    if (pos == string::npos) {
        string rest; getline(ss, rest);
        course.Cname = trim(rest);
        return course;
    }
    size_t afterCode = pos + ccode.size();
    string rest = trim(l.substr(afterCode));

    // tìm vị trí bắt đầu của phần thời gian (bắt đầu bằng chữ số)
    int timePos = -1;
    for (size_t i = 0; i < rest.size(); ++i) {
        if (isdigit((unsigned char)rest[i])) {
            size_t commaPos = rest.find(',', i);
            if (commaPos != string::npos && commaPos < i + 6) { timePos = (int)i; break; }
        }
    }

    if (timePos != -1) {
        course.Cname = trim(rest.substr(0, timePos));
        course.CtimeRaw = trim(rest.substr(timePos));
    } else {
        vector<string> parts;
        string tmp;
        stringstream s2(l);
        while (getline(s2, tmp, '\t')) parts.push_back(trim(tmp));
        if (parts.size() >= 4) {
            course.Cid = parts[0];
            course.Ccode = parts[1];
            course.Cname = parts[2];
            // combine the remaining as timeRaw (in case room separated)
            course.CtimeRaw = parts[3];
            if (parts.size() >= 5) course.CroomRaw = parts[4];
        } else {
            // if not, put entire rest as name
            course.Cname = rest;
        }
    }

    while (!course.CtimeRaw.empty() && course.CtimeRaw.back() == ';') course.CtimeRaw.pop_back();
    parseCourseTime(course);
    return course;
}

void loadStudentAccounts(const string &filename, vector<StudentAccount> &studentAccounts)
{
    ifstream file(filename);
    if (file.is_open()) {
        string line;
        while (getline(file, line)) {
            stringstream ss(line);
            StudentAccount account;
            getline(ss, account.Sid, '\t');
            getline(ss, account.password, '\t');
            account.Sid = trim(account.Sid);
            account.password = trim(account.password);
            if (!account.Sid.empty()) studentAccounts.push_back(account);
        }
        cout << "Loaded " << studentAccounts.size() << " student accounts." << endl;
        file.close();
    } else {
        cout << "Unable to open file: " << filename << endl;
    }
}

void loadStudentCourses(const string &filename, vector<StudentCourse> &studentCourses)
{
    ifstream file(filename);
    if (file.is_open()) {
        string line;
        while (getline(file, line)) {
            stringstream ss(line);
            StudentCourse sc;
            getline(ss, sc.Sid, '\t');
            getline(ss, sc.Cid, '\t');
            sc.Sid = trim(sc.Sid);
            sc.Cid = trim(sc.Cid);
            if (!sc.Sid.empty() && !sc.Cid.empty()) studentCourses.push_back(sc);
        }
        cout << "Loaded " << studentCourses.size() << " student courses." << endl;
        file.close();
    } else {
        cout << "Unable to open file: " << filename << endl;
    }
}

void loadCourses(const string &filename, vector<Course> &courses)
{
    ifstream file(filename);
    if (file.is_open()) {
        string line;
        while (getline(file, line)) {
            if (trim(line).empty()) continue;
            Course c = parseCourseLineFlexible(line);
            if (!c.Cid.empty()) courses.push_back(c);
        }
        cout << "Loaded " << courses.size() << " courses." << endl;
        file.close();
    } else {
        cout << "Unable to open file: " << filename << endl;
    }
}

string formatCourseTime(const Course &c) {
    if (!c.parsed) {
        string out = c.CtimeRaw.empty() ? "" : c.CtimeRaw;
        if (!c.CroomRaw.empty()) out += ", room " + c.CroomRaw;
        return out;
    }
    string out = dayNumberToName(c.dayInt) + " (" + to_string(c.dayInt) + "), " + ampmName(c.ampm);
    out += ", period " + to_string(c.startPeriod) + "-" + to_string(c.endPeriod);
    if (!c.weeks.empty()) {
        out += ", weeks: ";
        for (size_t i = 0; i < c.weeks.size(); ++i) {
            if (i) out += ",";
            out += c.weeks[i];
        }
    }
    if (!c.CroomRaw.empty()) out += ", room: " + c.CroomRaw;
    return out;
}

int dayNameToNumber(const string &s) {
    string lower;
    for (char ch : s) lower += (char)tolower((unsigned char)ch);
    if (lower == "monday" || lower == "mon" || lower == "thu 2" || lower == "2") return 2;
    if (lower == "tuesday" || lower == "tue" || lower == "mon1" || lower == "3") return 3;
    if (lower == "wednesday" || lower == "wed" || lower == "4") return 4;
    if (lower == "thursday" || lower == "thu" || lower == "5") return 5;
    if (lower == "friday" || lower == "fri" || lower == "6") return 6;
    if (lower == "saturday" || lower == "sat" || lower == "7") return 7;
    if (lower == "sunday" || lower == "sun" || lower == "1") return 1;
    // try numeric
    try { return stoi(s); } catch(...) { return -1; }
}

void printCourseRow(const Course &c) {
    string weekday = dayNumberToName(c.dayInt);
    string ampmStr = ampmName(c.ampm);
    string period = to_string(c.startPeriod) + "-" + to_string(c.endPeriod);

    string weekStr;
    for (size_t i = 0; i < c.weeks.size(); i++) {
        if (i) weekStr += ",";
        weekStr += c.weeks[i];
    }

    cout << left
         << setw(10) << c.Ccode << "|"
         << setw(25) << c.Cname << "|"
         << setw(10) << weekday << "|"
         << setw(10) << ampmStr << "|"
         << setw(10) << period << "|"
         << setw(20) << weekStr << "|"
         << setw(10) << c.CroomRaw << "\n";
}

void printScheduleHeader() {
    cout << "=============================================================================================\n";
    cout << left
         << setw(10) << "Code" << "|"
         << setw(25) << "Course" << "|"
         << setw(10) << "Week Day" << "|"
         << setw(10) << "AM/PM" << "|"
         << setw(10) << "Period" << "|"
         << setw(20) << "Week" << "|"
         << setw(10) << "Room" << "\n";
    cout << "---------------------------------------------------------------------------------------------\n";
}

void login(vector<StudentAccount> &studentAccounts) {
    string Sid, password;
    cout << "Enter Student ID: ";
    cin >> Sid;
    cout << "Enter Password: ";
    cin >> password;

    bool found = false;
    for (const auto &account : studentAccounts)
    {
        if (account.Sid == Sid && account.password == password)
        {
            found = true;
            currentSid = Sid;
            break;
        }
    }

    if (found)
    {
        cout << "Login successful! " << endl;
    }
    else
    {
        cout << "Invalid Student ID or Password." << endl;
    }
}

void viewScheduleWeek(const vector<StudentCourse>& studentCourses,
                      const vector<Course>& courses) {
    // Lấy danh sách courses của sinh viên
    vector<Course> enrolledCourses;
    for (const auto &sc : studentCourses) {
        if (sc.Sid != currentSid) continue;
        for (const auto &course : courses) {
            if (course.Cid == sc.Cid && course.parsed) {
                enrolledCourses.push_back(course);
                break;
            }
        }
    }

    if (enrolledCourses.empty()) {
        cout << "No courses found for Student ID: " << currentSid << endl;
        return;
    }

    // Tạo bảng lịch trống
    vector<string> days = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday"};
    map<int,int> dayToCol;
    dayToCol[2] = 0; dayToCol[3] = 1; dayToCol[4] = 2; dayToCol[5] = 3; dayToCol[6] = 4;

    
    vector<vector<string>> table(13, vector<string>(5, ""));

    for (auto &c : enrolledCourses) {
        if (dayToCol.find(c.dayInt) == dayToCol.end()) continue;
        int col = dayToCol[c.dayInt];
        int offset = (c.ampm) ? 6 : 0;
        for (int p = c.startPeriod; p <= c.endPeriod && p <= 6; p++) {
            int realPeriod = p + offset;
            if (realPeriod >= 1 && realPeriod <= 12) {
                table[realPeriod][col] = c.CroomRaw;
            }
        }
    }

    cout << "========================================================\n";
    cout << setw(4) << " ";
    for (auto& d : days) cout << "|" << setw(10) << d;
    cout << "\n--------------------------------------------------------\n";

    for (int p = 1; p <= 12; p++) {
        cout << setw(2) << p << " |";
        for (int j = 0; j < 5; j++) {
            cout << setw(10) << table[p][j] << "|";
        }
        cout << "\n";
    }
}

void viewSchedule(const vector<StudentCourse> &studentCourses,
                  const vector<Course> &courses,
                  const string &inputRaw) {
    string input = trim(inputRaw);
    if (input.empty()) {
        cout << "You must enter 'All' or a valid day.\n";
        return;
    }

    string lower;
    for (char ch : input) lower += (char)tolower((unsigned char)ch);

    if (lower == "all") {
        cout << "Weekly schedule for Student ID " << currentSid << ":\n";
        viewScheduleWeek(studentCourses, courses);
        return;
    }

    int dayNumber = dayNameToNumber(input);
    if (dayNumber == -1) {
        cout << "Invalid input. Please type 'All' or a valid day.\n";
        return;
    }

    // In lịch 1 ngày chi tiết (giữ như cũ)
    vector<Course> enrolledCourses;
    for (const auto &sc : studentCourses) {
        if (sc.Sid != currentSid) continue;
        for (const auto &course : courses) {
            if (course.Cid == sc.Cid && course.parsed) {
                enrolledCourses.push_back(course);
                break;
            }
        }
    }

    if (enrolledCourses.empty()) {
        cout << "No courses found for Student ID: " << currentSid << endl;
        return;
    }

    cout << "Schedule for Student ID " << currentSid
         << " on " << dayNumberToName(dayNumber) << ":\n";

    printScheduleHeader();
    bool any = false;
    for (auto &course : enrolledCourses) {
        if (course.dayInt == dayNumber) {
            printCourseRow(course);
            any = true;
        }
    }
    if (!any) cout << "No courses found.\n";
}



int main() {
    loadStudentAccounts("user_account.txt", studentAccounts);
    loadStudentCourses("student_registration.txt", studentCourses);
    loadCourses("course_schedule.txt", courses);

    login(studentAccounts);
    if (currentSid.empty()) return 0;
    cin.ignore(numeric_limits<streamsize>::max(), '\n'); 

    string choice;
    do {
        cout << "\nEnter a day (e.g. Monday, 5), 'All' to view all courses, or '0' to exit:\n";
        cout << "Input: ";
        getline(cin, choice);
        choice = trim(choice);

        if (choice == "0") break;

        viewSchedule(studentCourses, courses, choice);

    } while (choice != "0");

    return 0;
}
