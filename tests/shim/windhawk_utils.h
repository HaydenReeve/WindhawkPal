// Stands in for Windhawk's windhawk_utils.h in the unit tests. Settings read
// from fake::settings, and a missing key reads as empty.
#pragma once

#include <map>
#include <string>

namespace fake {
inline std::map<std::wstring, std::wstring> settings;
}

namespace WindhawkUtils {

class StringSetting {
   public:
    static StringSetting make(PCWSTR valueName) {
        StringSetting setting;
        auto it = fake::settings.find(valueName);
        if (it != fake::settings.end()) {
            setting.m_value = it->second;
        }
        return setting;
    }

    PCWSTR get() const { return m_value.c_str(); }

   private:
    std::wstring m_value;
};

}  // namespace WindhawkUtils
