//
// Created by cpasjuste on 28/09/23.
//

#include "skeleton/pemu.h"
#include "burner.h"
#include "pfbneo_utility.h"
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <cstdint>

// ---------- 全局翻译表（程序启动时加载一次） ----------
static std::unordered_map<std::string, std::string> g_ChineseGameMap;


// ---------- 从二进制 .bin 文件快速加载查找表 ----------
// 二进制格式 (版本1):
//   [4字节 Magic: 'C','G','L','T']
//   [4字节 Version (uint32_t LE) = 1]
//   [4字节 Entry Count (uint32_t LE)]
//   每条记录:
//     [2字节 Key长度 (uint16_t LE)]
//     [Key字节 (UTF-8)]
//     [2字节 Value长度 (uint16_t LE)]
//     [Value字节 (UTF-8)]
static bool LoadChineseGameListBinary(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return false;
    }

    std::streamsize fileSize = file.tellg();
    if (fileSize < 12)  // 文件头最小大小
    {
        return false;
    }
    file.seekg(0, std::ios::beg);

    // 读取文件头
    char magic[4];
    file.read(magic, 4);
    if (std::memcmp(magic, "CGLT", 4) != 0)
    {
        return false;
    }

    uint32_t version = 0;
    uint32_t count = 0;
    file.read(reinterpret_cast<char*>(&version), 4);
    file.read(reinterpret_cast<char*>(&count), 4);
    if (version != 1)
    {
        return false;
    }

    // 清空旧数据并预分配桶数量
    g_ChineseGameMap.clear();
    g_ChineseGameMap.reserve(count);

    // 读取每条记录
    for (uint32_t i = 0; i < count; i++)
    {
        uint16_t keyLen = 0;
        uint16_t valLen = 0;
        file.read(reinterpret_cast<char*>(&keyLen), 2);
        if (keyLen > 0)
        {
            std::string key(keyLen, '\0');
            file.read(&key[0], keyLen);
            file.read(reinterpret_cast<char*>(&valLen), 2);
            std::string val(valLen, '\0');
            file.read(&val[0], valLen);
            g_ChineseGameMap[key] = val;
        }
        else
        {
            // key长度为0，跳过value
            file.read(reinterpret_cast<char*>(&valLen), 2);
            if (valLen > 0)
            {
                std::string val(valLen, '\0');
                file.read(&val[0], valLen);
            }
        }
    }

    // 检查读取是否成功
    if (file.bad() || file.fail())
    {
        return false;
    }

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

        // 再次尝试 BurnDrvGetTextA 获取驱动中带的英文fullname
        const char* fullName = BurnDrvGetTextA(DRV_FULLNAME);
        if (fullName && fullName[0] != '\0')
        {
            return fullName;
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

PFBNEOUtility::GameInfo PFBNEOUtility::getGameInfo(const Game &game, const std::string& chineseFilePath) {
    // 懒加载：第一次调用时自动加载中文翻译表
    static bool s_chineseListLoaded = false;
    if (!s_chineseListLoaded && !chineseFilePath.empty())
    {
        LoadChineseGameListBinary(chineseFilePath);
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
