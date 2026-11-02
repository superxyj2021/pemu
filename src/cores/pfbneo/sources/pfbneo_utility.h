//
// Created by cpasjuste on 28/09/23.
//

#ifndef PEMU_PFBNEO_UTILITY_H
#define PEMU_PFBNEO_UTILITY_H

#include <string>

#define HARDWARE_PREFIX_ARCADE 0x12341234

class PFBNEOUtility {
public:
    struct GameInfo {
        std::string name;
        std::string manufacturer;
        std::string parent;
        std::string sysName;
        int sysId;
        int players;
		int genre;
		std::string date;
    };

    static int setDriverActive(const Game &game);

    static GameInfo getGameInfo(const Game &game, const std::string& chineseTxtPath);

    // path of the Chinese display-name translation table: an sd-side copy
    // (users can update it without rebuilding) or the copy bundled in the
    // nro romfs
    static std::string getChineseTxtPath(c2d::Io *io);

    // GBK (EUC-CN) -> UTF-8 lookup conversion (switch libc has no iconv)
    static std::string gbkToUtf8(const std::string &input);

    // true when the byte string is already valid UTF-8
    static bool isUtf8(const std::string &input);
};

#endif //PEMU_PFBNEO_UTILITY_H
