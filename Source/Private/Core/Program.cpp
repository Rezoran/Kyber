// Copyright BattleDash. All Rights Reserved.

#include <Core/Program.h>

#include <Base/Version.h>
#include <Base/Log.h>
//#include <Render/Renderer.h>
#include <Utilities/ErrorUtils.h>
#include <Utilities/PlatformUtils.h>
#include <Utilities/MemoryUtils.h>
#include <Hook/HookManager.h>
#include <SDK/SDK.h>
#include <Network/SocketManager.h>
#include <API/LanAPIService.h>

#include <MinHook/MinHook.h>

#include <Windows.h>
#include <cstdio>
#include <chrono>
#include <thread>
#include <limits>
#include <iostream>
#include <SDK/Modes.h>

#define OFFSET_CLIENT_STATE_CHANGE HOOK_OFFSET(0x140A8C7A0)
#define OFFSET_GET_SETTINGS_OBJECT HOOK_OFFSET(0x1401F7BD0)

Kyber::Program* g_program;

namespace Kyber
{
Program::Program(HMODULE module)
    : m_module(module)
    , m_api(nullptr)
    , m_server(nullptr)
    , m_clientState(ClientState_None)
    , m_joining(false)
{
    if (g_program || MH_Initialize() != MH_OK)
    {
        ErrorUtils::ThrowException("Initialization failed. Please restart Battlefront and try again!");
    }

    // Open a console
    AllocConsole();
    FILE* pFile;
    freopen_s(&pFile, "CONOUT$", "w", stdout);
    freopen_s(&pFile, "CONIN$", "r", stdin);

    // ANSI Colors
    HANDLE stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode;
    GetConsoleMode(stdoutHandle, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(stdoutHandle, dwMode);

    SetConsoleTitleA(("Kyber v" + KYBER_VERSION).c_str());

    new std::thread(&Program::InitializationThread, this);
}

Program::~Program()
{
    KYBER_LOG(LogLevel::Info, "Destroying Kyber");
    HookManager::RemoveHooks();
    delete m_server;
    delete m_api;
    //delete g_renderer;
    KYBER_LOG(LogLevel::Info, "Done Destroying Kyber");
}

DWORD WINAPI Program::InitializationThread()
{
    KYBER_LOG(LogLevel::Info, "Initializing...");
    KYBER_LOG(LogLevel::Info, " _____     _   _   _     ____           _ ");
    KYBER_LOG(LogLevel::Info, "| __  |___| |_| |_| |___|    \\ ___ ___| |_");
    KYBER_LOG(LogLevel::Info, "| __ -| .'|  _|  _| | -_|  |  | .'|_ -|   |");
    KYBER_LOG(LogLevel::Info, "|_____|__,|_| |_| |_|___|____/|__,|___|_|_|");

    InitializeGameHooks();

    m_api = new LanApiService();
    //g_renderer = new Renderer();
    m_server = new Server();

    KYBER_LOG(LogLevel::Info, "Initialized Kyber v" << KYBER_VERSION);
    KYBER_LOG(LogLevel::Warning, "Press [INS]  to join a server");
    KYBER_LOG(LogLevel::Warning, "Press [POS1] to host a server");
    KYBER_LOG(LogLevel::Warning, "Press [DEL]  to open player menu");
    KYBER_LOG(LogLevel::Warning, "Press [F10]  to refill ai");
    KYBER_LOG(LogLevel::Warning, "Press [H]    to print this help");

    while (1)
    {
        // print help
        if (GetAsyncKeyState(72) & 1)
        {
            KYBER_LOG(LogLevel::Info, "Initialized Kyber v" << KYBER_VERSION);
            KYBER_LOG(LogLevel::Warning, "Press [INS]  to join a server");
            KYBER_LOG(LogLevel::Warning, "Press [POS1] to host a server");
            KYBER_LOG(LogLevel::Warning, "Press [DEL]  to open player menu");
            KYBER_LOG(LogLevel::Warning, "Press [F10]  to refill ai");
            KYBER_LOG(LogLevel::Warning, "Press [H]    to print this help");
        }
        // Host a server
        if (GetAsyncKeyState(VK_HOME) & 1)
        {
#define IM_ARRAYSIZE(_ARR)((int)(sizeof(_ARR) / sizeof(*(_ARR)))) // Size of a static C-style array. Don't use on pointers!

            KYBER_LOG(LogLevel::Debug, "Key pressed: POS1");
            if (!this->m_server->m_running)
            {
                static GameMode currentMode = { "", "Mode", {}, {} };
                static GameLevel currentLevel = { "", "Level" };
                KYBER_LOG(LogLevel::Warning, "Select a game mode:");

                // ask and retrieve mode
                for (int n = 0; n < IM_ARRAYSIZE(s_game_modes); n++)
                {
                    KYBER_LOG(LogLevel::Info, n << ": " << s_game_modes[n].name);
                }
                std::string mode_index;
                std::cin.clear();
                std::getline(std::cin, mode_index, '\n');
                if (mode_index.empty())
                {
                    KYBER_LOG(LogLevel::Error, "Please enter a valid number. - returning to main menu");
                    continue;
                }
                try
                {
                    currentMode = s_game_modes[std::stoi(mode_index)];
                }
                catch (std::invalid_argument const& ex)
                {
                    KYBER_LOG(LogLevel::Error, "Please enter a valid number. - returning to main menu");
                    continue;
                }
                KYBER_LOG(LogLevel::Info, "Selected mode: " << currentMode.name);

                // ask and retrieve level
                KYBER_LOG(LogLevel::Warning, "Select a level:");
                for (int i = 0; i < currentMode.levels.size(); i++)
                {
                    GameLevel level = GetGameLevel(currentMode, currentMode.levels[i]);
                    KYBER_LOG(LogLevel::Info, i << ": " << level.name);
                }
                std::string level_index;
                std::cin.clear();
                std::getline(std::cin, level_index, '\n');
                if (level_index.empty())
                {
                    KYBER_LOG(LogLevel::Error, "Please enter a valid number. - returning to main menu");
                    continue;
                }
                try
                {
                    currentLevel = GetGameLevel(currentMode, currentMode.levels[std::stoi(level_index)]);
                }
                catch (std::invalid_argument const& ex)
                {
                    KYBER_LOG(LogLevel::Error, "Please enter a valid number. - returning to main menu");
                    continue;
                }
                KYBER_LOG(LogLevel::Info, "Selected level: " << currentLevel.name);

                // set max players
                static int maxPlayers = 40;

                if (strcmp(currentMode.name, "Mode") != 0 && strcmp(currentLevel.name, "Level") != 0)
                {
                    this->m_server->Start(currentLevel.level, currentMode.mode, maxPlayers);
                }
                while (this->m_clientState != ClientState_Ingame)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                // setup server
                KYBER_LOG(LogLevel::Info, "Server started");
                AutoPlayerSettings* aiSettings = Settings<AutoPlayerSettings>("AutoPlayers");
                WSGameSettings* wsSettings = Settings<WSGameSettings>("Whiteshark");
                GameSettings* gameSettings = Settings<GameSettings>("Game");
                aiSettings->ForcedServerAutoPlayerCount = -1;
                aiSettings->UpdateAI = true;
                aiSettings->ServerPlayersIgnoreClientPlayers = false;
                wsSettings->AutoBalanceTeamsOnNeutral = true;
                wsSettings->EventWelcomeTimer = 15;

                KYBER_LOG(LogLevel::Warning, "Select Server difficulty index: ");
                KYBER_LOG(LogLevel::Info, "0: Easy");
                KYBER_LOG(LogLevel::Info, "1: Medium (default)");
                KYBER_LOG(LogLevel::Info, "2: Hard");
                std::string defficulty;
                std::cin.clear();
                std::getline(std::cin, defficulty, '\n');
                if (defficulty.empty())
                {
                    gameSettings->DifficultyIndex = 1;
                    KYBER_LOG(LogLevel::Warning, "Nothing specified, setting to " << gameSettings->DifficultyIndex);
                    continue;
                }
                else
                {
                    try
                    {
                        gameSettings->DifficultyIndex = std::stoi(defficulty);
                    }
                    catch (std::invalid_argument const& ex)
                    {
                        KYBER_LOG(LogLevel::Error, "Please enter a valid number. - returning to main menu");
                        continue;
                    }
                }
                KYBER_LOG(LogLevel::Info, "Server difficulty index: " << gameSettings->DifficultyIndex);

                // ask for the game start
                while (true)
                {
                    KYBER_LOG(LogLevel::Info, "Press [s] to start the game or [p] to show player menu");
                    std::string c;
                    std::cin.clear();
                    std::getline(std::cin, c, '\n');
                    if (c == "p")
                    {
                        while (1)
                        {
                            KYBER_LOG(LogLevel::Info, "Player list:");
                            ServerPlayerManager* playerManager = this->m_server->m_playerManager;
                            std::vector players = std::vector<ServerPlayer*>();
                            if (playerManager)
                            {
                                for (ServerPlayer* player : playerManager->m_players)
                                {
                                    if (player && !player->m_isAIPlayer)
                                    {
                                        players.push_back(player);
                                    }
                                }
                                for (int i = 0; i < players.size(); i++)
                                {
                                    ServerPlayer* player = players[i];
                                    KYBER_LOG(LogLevel::Info, i << ": " << player->m_name << (player->m_teamId == 1 ? " LIGHT-SIDE" : " DARK-SIDE"));
                                }
                            }
                            KYBER_LOG(LogLevel::Info, "Put in the index of the player to switch sides or q to exit or r to reprint");
                            std::string d;
                            std::cin.clear();
                            std::getline(std::cin, d, '\n');
                            if (d == "q")
                                break;
                            else if (d == "r")
                                continue;
                            else
                            {
                                int index = std::stoi(d);
                                ServerPlayer* player = players[index];
                                this->m_server->SetPlayerTeam(player, player->m_teamId == 1 ? 2 : 1);
                            }
                        }
                    }
                    else if (c == "s")
                    {
                        aiSettings->ForceFillGameplayBotsTeam1 = 30;
                        aiSettings->ForceFillGameplayBotsTeam2 = 29;
                        KYBER_LOG(LogLevel::Info, "Game started");
                        break;
                    }
                }
            }
            else
            {
                KYBER_LOG(LogLevel::Warning, "Please stop your server in order to join one.");
                KYBER_LOG(LogLevel::Warning, "You can do so by pressing 'Quit' in the pause menu.");
            }
        }
        // join a server
        if (GetAsyncKeyState(VK_INSERT) & 1)
        {
            KYBER_LOG(LogLevel::Debug, "Key pressed: INS");
            // ask for ip
            if (!this->m_server->m_running)
            {
                KYBER_LOG(LogLevel::Warning, "Enter the IP of the server you want to join:");
                std::string ip;
                std::cin.clear();
                std::getline(std::cin, ip, '\n');
                if (!ip.empty())
                {
                    KYBER_LOG(LogLevel::Info, "Joining server at " << ip);
                    ClientSettings* clientSettings = Settings<ClientSettings>("Client");

                    clientSettings->ServerIp = new char[ip.length() + 1];
                    strcpy(clientSettings->ServerIp, ip.c_str());

                    SocketSpawnInfo info(false, "", "");
                    this->m_server->m_socketSpawnInfo = info;
                    this->m_joining = true;
                    this->ChangeClientState(ClientState_Startup);
                }
                else
                {
                    KYBER_LOG(LogLevel::Warning, "Please enter a valid IP address.");
                }
            }
            else
            {
                KYBER_LOG(LogLevel::Warning, "Please stop your server in order to join one.");
                KYBER_LOG(LogLevel::Warning, "You can do so by pressing 'Quit' in the pause menu.");
            }
        }
        // player menu
        if (GetAsyncKeyState(VK_DELETE) & 1)
        {
            KYBER_LOG(LogLevel::Debug, "Key pressed: DEL");
            if (this->m_server->m_running)
            {
                while (1)
                {
                    KYBER_LOG(LogLevel::Info, "Player list:");
                    ServerPlayerManager* playerManager = this->m_server->m_playerManager;
                    std::vector players = std::vector<ServerPlayer*>();
                    if (playerManager)
                    {
                        for (ServerPlayer* player : playerManager->m_players)
                        {
                            if (player && !player->m_isAIPlayer)
                            {
                                players.push_back(player);
                            }
                        }
                        for (int i = 0; i < players.size(); i++)
                        {
                            ServerPlayer* player = players[i];
                            KYBER_LOG(
                                LogLevel::Info, i << ": " << player->m_name << (player->m_teamId == 1 ? " LIGHT-SIDE" : " DARK-SIDE"));
                        }
                    }
                    KYBER_LOG(LogLevel::Info, "Put in 's' to switch or 'k' to kick followed by the index of the player or q to exit or r to reprint");
                    std::string d;
                    std::cin.clear();
                    std::getline(std::cin, d, '\n');
                    if (d == "q")
                        break;
                    else if (d == "r")
                        continue;
                    else
                    {
                        int index = -1;
                        try
                        {
                           index = std::stoi(d.substr(1));
                        }
                        catch (std::invalid_argument const& ex)
                        {
                            KYBER_LOG(LogLevel::Error, "Please enter a valid number. - reprinting");
                            continue;
                        }
                        if (d[0] == 'k')//TODO boundary check
                        {
                            ServerPlayer* player = players[index];
                            this->m_server->KickPlayer(player, "You have been kicked.");
                        }
                        else if (d[0] == 's')
                        {
                            ServerPlayer* player = players[index];
                            this->m_server->SetPlayerTeam(player, player->m_teamId == 1 ? 2 : 1);
                        }
                    }
                }//while
            }//if server running
        }
        // refill ai
        if (GetAsyncKeyState(VK_F10) & 1)
        {
            KYBER_LOG(LogLevel::Debug, "Key pressed: F10");
            if (this->m_server->m_running)
            {
                AutoPlayerSettings* aiSettings = Settings<AutoPlayerSettings>("AutoPlayers");
                aiSettings->ForcedServerAutoPlayerCount = -1;
                aiSettings->UpdateAI = true;
                aiSettings->ServerPlayersIgnoreClientPlayers = false;
                aiSettings->ForceFillGameplayBotsTeam1 = 30;
                aiSettings->ForceFillGameplayBotsTeam2 = 29;
                KYBER_LOG(LogLevel::Info, "AI refilled");
            }
            else
            {
                KYBER_LOG(LogLevel::Warning, "Please start your server in order to refill AI.");
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}

HookTemplate program_hook_offsets[] = {
    { OFFSET_CLIENT_STATE_CHANGE, ClientStateChangeHk },
    { OFFSET_GET_SETTINGS_OBJECT, GetSettingsObjectHk },
};

void Program::InitializeGameHooks()
{
    for (HookTemplate& hook : program_hook_offsets)
    {
        HookManager::CreateHook(hook.offset, hook.hook);
    }
    Hook::ApplyQueuedActions();
}

__int64 ClientStateChangeHk(__int64 inst, ClientState currentClientState, ClientState lastClientState)
{
    static const auto trampoline = HookManager::Call(ClientStateChangeHk);
    g_program->m_clientState = currentClientState;
    KYBER_LOG(LogLevel::DebugPlusPlus, "Client state changed to " << currentClientState);
    Server* server = g_program->m_server;
    if (currentClientState == ClientState_Startup)
    {
        if (server->m_running)
        {
            server->Stop();

            GameSettings* gameSettings = Settings<GameSettings>("Game");
            gameSettings->Level = "Levels/FrontEnd/FrontEnd";
            gameSettings->DefaultLayerInclusion = "";
        }
        else
        {
            if (!g_program->m_joining)
            {
                Settings<ClientSettings>("Client")->ServerIp = "";
            }
            else
            {
                g_program->m_joining = false;
            }
        }
    }
    else if (currentClientState == ClientState_Ingame && server->m_running)
    {
        server->InitializeGameSettings();
    }
    return trampoline(inst, currentClientState, lastClientState);
}

__int64 GetSettingsObjectHk(__int64 inst, const char* identifier)
{
    static const auto trampoline = HookManager::Call(GetSettingsObjectHk);
    return trampoline(inst, identifier);
}
} // namespace Kyber