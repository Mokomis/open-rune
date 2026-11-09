#pragma once
#include <string>
namespace RuneUpdates {
struct Result {
    bool busy=false,available=false;
    std::string message="Updates have not been checked.",version,notes,url;
};
Result Inspect(int status,const std::string& body);
Result Fetch(); // Bounded HTTPS request, never installs or runs downloaded code.
Result Current();
void Check(bool manual=false); // Non-blocking; automatic once per process, manual cooldown.
bool Automatic();
void SetAutomatic(bool enabled);
std::string Identity();
bool OpenPage(const std::string& url); // Only this repository's releases pages.
int Probe();
}
