#pragma once
#include <string>
namespace i18n {
const char* c(const char* english);
std::string text(const std::string& english);
void select(const std::string& code);
const char* preference();
}

