//
// Created by cpasjuste on 28/09/23.
//

#include "skeleton/pemu.h"
#include "burner.h"
#include "pfbneo_utility.h"
#include "gbk_unicode_table.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <cstdint>

// ---------- 全局翻译表（程序启动时加载一次） ----------
static std::unordered_map<std::string, std::string> g_ChineseGameMap;


// ---------- GBK (EUC-CN) -> UTF-8 ----------
// 用生成表做查表转换（switch 工具链的 libc 未编译 iconv，见 gbk_unicode_table.h）
static std::string GbkToUtf8(const std::string &input) {
    std::string out;
    out.reserve(input.size() * 2);
    size_t i = 0;
    while (i < input.size()) {
        unsigned char c = (unsigned char) input[i];
        if (c < 0x80) {
            out += (char) c;
            i++;
            continue;
        }
        // GBK 双字节：lead 0x81-0xFE，trail 0x40-0xFE（0x7F 除外）
        if (c >= pemu::GBK_UNICODE_TABLE_LEAD_MIN && c <= 0xFE && i + 1 < input.size()) {
            unsigned char lo = (unsigned char) input[i + 1];
            if (lo >= pemu::GBK_UNICODE_TABLE_TRAIL_MIN && lo != 0x7F) {
                int idx = (c - pemu::GBK_UNICODE_TABLE_LEAD_MIN) * pemu::GBK_UNICODE_TABLE_COLS
                          + (lo >= 0x7F ? lo - 0x41 : lo - pemu::GBK_UNICODE_TABLE_TRAIL_MIN);
                unsigned short cp = pemu::GBK_UNICODE_TABLE[idx];
                if (cp != 0) {
                    if (cp < 0x80) {
                        out += (char) cp;
                    } else if (cp < 0x800) {
                        out += (char) (0xC0 | (cp >> 6));
                        out += (char) (0x80 | (cp & 0x3F));
                    } else {
                        out += (char) (0xE0 | (cp >> 12));
                        out += (char) (0x80 | ((cp >> 6) & 0x3F));
                        out += (char) (0x80 | (cp & 0x3F));
                    }
                    i += 2;
                    continue;
                }
            }
        }
        // 无法映射的字节直接跳过
        i++;
    }
    return out;
}


// ---------- 从文本 .glt 文件加载查找表 ----------
// 文件格式（FinalBurn Neo 的中文翻译模板，GBK/GB2312 编码，CRLF 换行）：
//   // 注释行
//   <driver>\t<显示名>
// 文件位置：与 nro 同级目录下的 localisation/Chinese (Simplified)_gamelist.glt
static bool LoadChineseGameListText(const std::string &filePath)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        printf("LoadChineseGameListText: could not open %s\n", filePath.c_str());
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string utf8 = GbkToUtf8(content);

    g_ChineseGameMap.clear();
    std::string line;
    std::stringstream ss(utf8);
    while (std::getline(ss, line)) {
        // 去掉 CRLF 的 \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        // 跳过空行和注释
        if (line.empty() || line.compare(0, 2, "//") == 0) {
            continue;
        }
        auto tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, tab);
        std::string val = line.substr(tab + 1);
        // 去掉 key 两侧空白
        size_t b = key.find_first_not_of(" \t");
        size_t e = key.find_last_not_of(" \t");
        if (b == std::string::npos) {
            continue;
        }
        key = key.substr(b, e - b + 1);
        if (key.empty() || val.empty()) {
            continue;
        }
        g_ChineseGameMap[key] = val;
    }

    printf("LoadChineseGameListText: %s, entries: %zu\n", filePath.c_str(), g_ChineseGameMap.size());
    return !g_ChineseGameMap.empty();
}

// ---------- 根据 game.path（去掉扩展名）查找中文翻译名 ----------
// 返回值：找到则返回中文名称字符串；未找到则返回空字符串 ""
static std::string GetChineseNameFromPath(const std::string& gamePath)
{
    auto it = g_ChineseGameMap.find(gamePath);
    if (it != g_ChineseGameMap.end())
    {
        return it->second;
    }
    return "";
}

// ---------- 获取游戏的显示名称（核心函数） ----------
// 逻辑优先级：
//   1. 如果 game.name != game.path，直接返回 game.name（不做任何改动）
//   2. 如果 game.name == game.path：
//      a) 先通过 game.path（去掉扩展名）在 gamelist 中查找中文翻译
//      b) 如果找到翻译，返回中文翻译名
//      c) 如果未找到翻译，尝试 BurnDrvGetTextA(DRV_FULLNAME) 获取名称
//      d) 如果 BurnDrvGetTextA 也返回空，则不做变化，返回 game.name
//
// 使用方式：将 getGameInfo 中原来的
//   gameInfo.name = (game.name == game.path) ? ... : game.name;
// 替换为：
//   gameInfo.name = GetGameDisplayName(game);
//
// 注意：调用 GetGameDisplayName 前，getGameInfo 会通过 chineseFilePath 参数
// 自动加载中文翻译表（懒加载，仅首次调用时执行）。

static std::string GetGameDisplayName(const Game& game)
{
	// 只要是rom文件名不为空就去获取
    if (!game.path.empty())
    {
		std::string zipName = c2d::Utility::removeExt(game.path);
		if (game.system.id == HARDWARE_PREFIX_COLECO) {
			zipName = "cv_" + zipName;
		} else if (game.system.id == HARDWARE_PREFIX_SEGA_GAME_GEAR) {
			zipName = "gg_" + zipName;
		} else if (game.system.id == HARDWARE_PREFIX_SEGA_MEGADRIVE) {
			zipName = "md_" + zipName;
		} else if (game.system.id == HARDWARE_PREFIX_MSX) {
			zipName = "msx_" + zipName;
		} else if (game.system.id == HARDWARE_PCENGINE_PCENGINE) {
			zipName = "pce_" + zipName;
		} else if (game.system.id == HARDWARE_PREFIX_SEGA_SG1000) {
			zipName = "sg1k_" + zipName;
		} else if (game.system.id == HARDWARE_PCENGINE_SGX) {
			zipName = "sgx_" + zipName;
		} else if (game.system.id == HARDWARE_PREFIX_SEGA_MASTER_SYSTEM) {
			zipName = "sms_" + zipName;
		} else if (game.system.id == HARDWARE_PCENGINE_TG16) {
			zipName = "tg_" + zipName;
		} else if (game.system.id == HARDWARE_PREFIX_SPECTRUM) {
			zipName = "spec_" + zipName;
		} else if (game.system.id == HARDWARE_PREFIX_NES) {
			zipName = "nes_" + zipName;
		} else if (game.system.id == HARDWARE_PREFIX_FDS) {
			zipName = "fds_" + zipName;
		} else if (game.system.id == HARDWARE_PREFIX_CHANNELF) {
			zipName = "chf_" + zipName;
		} else if (game.system.id == HARDWARE_PREFIX_NGP) {
			zipName = "ngp_" + zipName;
		} else if (game.system.id == HARDWARE_PREFIX_SNES) {
			zipName = "snes_" + zipName;
		} else if (game.system.id == HARDWARE_PREFIX_GBA) {
			zipName = "gba_" + zipName;
		}
		
        // 优先尝试查找中文翻译
        std::string chineseName = GetChineseNameFromPath(zipName);
        if (!chineseName.empty())
        {
            return chineseName;  // 找到翻译，直接使用
        }
		
		// 如果是直接用的文件名，继续找驱动中的名字
        if (game.path == game.name)
        {
			// 再次尝试 BurnDrvGetTextA 获取驱动中带的英文fullname
			const char* fullName = BurnDrvGetTextA(DRV_FULLNAME);
			if (fullName && fullName[0] != '\0')
			{
				return fullName;
			}
            return game.name;  // 找到翻译，直接使用
        }
    }

    // 回退：返回原始 game.name（不做变化）
    return game.name;
}


int PFBNEOUtility::setDriverActive(const Game &game) {
    std::string zipName = c2d::Utility::removeExt(game.path);
    if (game.system.id == HARDWARE_PREFIX_COLECO) {
        zipName = "cv_" + zipName;
    } else if (game.system.id == HARDWARE_PREFIX_SEGA_GAME_GEAR) {
        zipName = "gg_" + zipName;
    } else if (game.system.id == HARDWARE_PREFIX_SEGA_MEGADRIVE) {
        zipName = "md_" + zipName;
    } else if (game.system.id == HARDWARE_PREFIX_MSX) {
        zipName = "msx_" + zipName;
    } else if (game.system.id == HARDWARE_PCENGINE_PCENGINE) {
        zipName = "pce_" + zipName;
    } else if (game.system.id == HARDWARE_PREFIX_SEGA_SG1000) {
        zipName = "sg1k_" + zipName;
    } else if (game.system.id == HARDWARE_PCENGINE_SGX) {
        zipName = "sgx_" + zipName;
    } else if (game.system.id == HARDWARE_PREFIX_SEGA_MASTER_SYSTEM) {
        zipName = "sms_" + zipName;
    } else if (game.system.id == HARDWARE_PCENGINE_TG16) {
        zipName = "tg_" + zipName;
    } else if (game.system.id == HARDWARE_PREFIX_SPECTRUM) {
        zipName = "spec_" + zipName;
    } else if (game.system.id == HARDWARE_PREFIX_NES) {
        zipName = "nes_" + zipName;
    } else if (game.system.id == HARDWARE_PREFIX_FDS) {
        zipName = "fds_" + zipName;
    } else if (game.system.id == HARDWARE_PREFIX_CHANNELF) {
        zipName = "chf_" + zipName;
    } else if (game.system.id == HARDWARE_PREFIX_NGP) {
        zipName = "ngp_" + zipName;
    } else if (game.system.id == HARDWARE_PREFIX_SNES) {
        zipName = "snes_" + zipName;
    } else if (game.system.id == HARDWARE_PREFIX_GBA) {
        zipName = "gba_" + zipName;
    }

    for (unsigned int i = 0; i < nBurnDrvCount + 1; i++) {
        nBurnDrvActive = i;
        if (nBurnDrvActive >= nBurnDrvCount) break;
        if (zipName == BurnDrvGetTextA(DRV_NAME)) {
            break;
        }
    }

    return (int) nBurnDrvActive;
}

PFBNEOUtility::GameInfo PFBNEOUtility::getGameInfo(const Game &game, const std::string& chineseTxtPath) {
    // 懒加载：第一次调用时自动加载中文翻译表（nro 同级
    // localisation/Chinese (Simplified)_gamelist.glt，GBK 文本）。
    static bool s_chineseListLoaded = false;
    if (!s_chineseListLoaded)
    {
        LoadChineseGameListText(chineseTxtPath);
        s_chineseListLoaded = true;
    }

    auto driver = setDriverActive(game);
    GameInfo gameInfo;
	if (driver < nBurnDrvCount) 
	{
        // gameInfo.name = BurnDrvGetTextA(DRV_FULLNAME) ? BurnDrvGetTextA(DRV_FULLNAME) : "";
		// superxyj modified
		// 根据游戏文件名去尝试获取中文游戏完整名称
		gameInfo.name = GetGameDisplayName(game);
        gameInfo.parent = BurnDrvGetTextA(DRV_PARENT) ? BurnDrvGetTextA(DRV_PARENT) : "";
        gameInfo.manufacturer = BurnDrvGetTextA(DRV_MANUFACTURER) ? BurnDrvGetTextA(DRV_MANUFACTURER) : "";
        gameInfo.players = BurnDrvGetMaxPlayers();
		gameInfo.date = BurnDrvGetTextA(DRV_DATE) ? BurnDrvGetTextA(DRV_DATE) : "";
		gameInfo.genre = BurnDrvGetGenreFlags();

        //
        unsigned int id = BurnDrvGetHardwareCode();
        unsigned int prefix = ((id | HARDWARE_PREFIX_CARTRIDGE) ^ HARDWARE_PREFIX_CARTRIDGE) & 0xff000000;
        if (prefix == HARDWARE_PREFIX_SNK) {
            gameInfo.sysId = (int) prefix;
            gameInfo.sysName = "Neo Geo";
        } else if (prefix == HARDWARE_PREFIX_CPS2) {
            gameInfo.sysId = (int) prefix;
            gameInfo.sysName = "Capcom Play System 2";
        } else if (prefix == HARDWARE_PREFIX_CPS3) {
            gameInfo.sysId = (int) prefix;
            gameInfo.sysName = "Capcom Play System 3";
        } else if (prefix == HARDWARE_PREFIX_CAPCOM) {
            gameInfo.sysId = (int) prefix;
            gameInfo.sysName = "Capcom Play System 1";
        } else {
            gameInfo.sysId = (int) prefix;
            switch (prefix) {
                case HARDWARE_PREFIX_MISC_PRE90S:
                    gameInfo.sysId = 0x43214321;
                    gameInfo.sysName = TEXT_ROM_SYSTEM_PRE_90S;
                    break;
                case HARDWARE_PREFIX_SEGA:
                    gameInfo.sysName = "Sega Arcade";
                    break;
                case HARDWARE_PREFIX_KONAMI:
                    gameInfo.sysName = "Konami Arcade";
                    break;
                case HARDWARE_PREFIX_TOAPLAN:
                    gameInfo.sysName = "Toaplan Arcade";
                    break;
                case HARDWARE_PREFIX_CAVE:
                    gameInfo.sysName = "Cave Arcade";
                    break;
                case HARDWARE_PREFIX_IGS_PGM:
                    gameInfo.sysName = "PolyGame Master";
                    break;
                case HARDWARE_PREFIX_IGS_PGM2:
                    gameInfo.sysName = "PolyGame Master 2";
                    break;
				case HARDWARE_PREFIX_MISC_POST90S:
                    gameInfo.sysName = TEXT_ROM_SYSTEM_POST_90S;
                    break;
                case HARDWARE_PREFIX_TAITO:
                    gameInfo.sysName = "Taito Arcade";
                    break;
                case HARDWARE_PREFIX_PSIKYO:
                    gameInfo.sysName = "Psikyo Arcade";
                    break;
                case HARDWARE_PREFIX_KANEKO:
                    gameInfo.sysName = "Kaneko Arcade";
                    break;
                case HARDWARE_PREFIX_PACMAN:
                    gameInfo.sysName = "Pac-Man Arcade";
                    break;
                case HARDWARE_PREFIX_GALAXIAN:
                    gameInfo.sysName = "Galaxian Arcade";
                    break;
                case HARDWARE_PREFIX_IREM:
                    gameInfo.sysName = "Irem Arcade";
                    break;
                case HARDWARE_PREFIX_DATAEAST:
                    gameInfo.sysName = "Data East Arcade";
                    break;
                case HARDWARE_PREFIX_CAPCOM_MISC:
                    gameInfo.sysName = "Capcom Miscellaneous";
                    break;
                case HARDWARE_PREFIX_SETA:
                    gameInfo.sysName = "Seta Arcade";
                    break;
                case HARDWARE_PREFIX_TECHNOS:
                    gameInfo.sysName = "Technos Arcade";
                    break;
                case HARDWARE_PREFIX_MIDWAY:
                    gameInfo.sysName = "Midway Arcade";
                    break;
                default:
                    // consoles games
                    break;
            }
        }

        return gameInfo;
    }

	gameInfo.name = "UnsupportedROM";
    return gameInfo;
}
