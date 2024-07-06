#include <Mod/CppUserModBase.hpp>

#include <Unreal/UObject.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/UnrealFlags.hpp>

#include "discord_rpc.h"

#define DISCORD_APPLICATION_ID "1158533300550369470"
#define PAYDAY_3_STEAM_APPID "1272080"

#define WITH_JOIN

struct FSBZPartyMember
{
    /* ... */
};

enum Status
{
    GameStarting,
    InMenu,
    // InMatchMaking,
    InPrePlanning,
    InHeist
};

std::wstring CharToWString(const char *src)
{
    return std::wstring(src, src + strlen(src));
}

std::string WCharToString(const wchar_t *src)
{
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(src); // that shit is deprecated but who care (not me)
}

const char *WCharToChar(const wchar_t *src)
{
    return WCharToString(src).c_str();
}

class DiscordMod : public CppUserModBase
{
public:
    Status status = Status::GameStarting;
    time_t startTimestamp = std::time(0);

    DiscordMod() : CppUserModBase()
    {
        ModName = STR("Discord");
        ModVersion = STR("1.0");
        ModDescription = STR("Discord Rich Presence Mod.");
        ModAuthors = STR("Mathi");

        DiscordEventHandlers handlers;
        memset(&handlers, 0, sizeof(handlers));
        handlers.ready = [](const DiscordUser *request)
        { Output::send<LogLevel::Verbose>(STR("[Discord:ready] {}#{} - {}\n"), CharToWString(request->username), CharToWString(request->discriminator), CharToWString(request->userId)); };
        handlers.disconnected = [](int errorCode, const char *message)
        { Output::send<LogLevel::Verbose>(STR("[Discord:disconnected] code={} message={}\n"), errorCode, CharToWString(message)); };
        handlers.errored = [](int errorCode, const char *message)
        { Output::send<LogLevel::Verbose>(STR("[Discord:errored] code={} message={}\n"), errorCode, CharToWString(message)); };
#ifdef WITH_JOIN
        handlers.joinGame = [](const char *joinSecret)
        {
            Output::send<LogLevel::Verbose>(STR("[Discord:joinGame] secret={}\n"), CharToWString(joinSecret));
            // by referring to MoolahNet, joining is pain and we dont have whats needed in joinSecret
            // also joinsecret is limited to 128 bytes by discord and this might not be enough data
            // also joining party and joining heist is different and needs to be handled differently?
        };
        handlers.joinRequest = [](const DiscordUser *request)
        {
            Output::send<LogLevel::Verbose>(STR("[Discord:joinRequest] {}#{} - {}\n"), CharToWString(request->username), CharToWString(request->discriminator), CharToWString(request->userId));
            Discord_Respond(request->userId, DISCORD_REPLY_IGNORE);
        };
#endif

        Discord_Initialize(DISCORD_APPLICATION_ID, &handlers, 1, PAYDAY_3_STEAM_APPID);

        Output::send<LogLevel::Verbose>(STR("[Discord] Init.\n"));
    }

    ~DiscordMod() override
    {
        Discord_Shutdown();
    }

    auto on_unreal_init() -> void override
    {
        Unreal::Hook::RegisterBeginPlayPostCallback(
            [=](Unreal::AActor *Context)
            {
                auto classPrivate = Context->GetClassPrivate();
                if (classPrivate == nullptr)
                    return;
                auto name = classPrivate->GetName();
                // Output::send<LogLevel::Verbose>(STR("[Discord] BeginPlayPostCallback: {}\n"), name);
                // TODO: mode precise timers
                if (name == STR("BP_GameModeMainMenu_C"))
                {
                    status = Status::InMenu;
                    startTimestamp = std::time(0);
                }
                else if (name == STR("SBZBeaconActionPhaseClient"))
                {
                    status = Status::InPrePlanning;
                    startTimestamp = std::time(0);
                }
                else if (name == STR("BP_PlayerController_C"))
                {
                    status = Status::InHeist;
                    startTimestamp = std::time(0);
                }
            });
    }

    // lazy delay for on_update
    uint8_t on_update_id = 0;

    auto on_update() -> void override
    {
        if (on_update_id != 255)
        {
            on_update_id++;
            return;
        }
        else
            on_update_id = 0;

        DiscordRichPresence discordPresence;
        memset(&discordPresence, 0, sizeof(discordPresence));

        RC::Unreal::UObject *SBZPartyManager = nullptr;
        int partySize = 1;

        switch (status)
        {
        case Status::GameStarting:
            return; // ignore, wait until a real status is set
        case Status::InMenu:
            SBZPartyManager = Unreal::UObjectGlobals::FindFirstOf(STR("SBZPartyManager"));
            if (SBZPartyManager != nullptr)
            {
                auto PartyMembers = SBZPartyManager->GetValuePtrByPropertyNameInChain<Unreal::TArray<FSBZPartyMember>>(STR("PartyMembers"));
                if (PartyMembers != nullptr)
                    partySize = PartyMembers->Num();
            }

            discordPresence.state = "In Menu";
            discordPresence.partySize = partySize;
            discordPresence.partyMax = 4; // hardcoded for now

            break;
        case Status::InPrePlanning:
            discordPresence.state = "In Pre-Planning";

            // TODO: get heist ID for largeImageKey
            // TODO: get heister name for smallImageKey

            break;
        case Status::InHeist:
            std::optional<StringType> heist_name = std::nullopt;
            std::optional<StringType> difficulty_name = std::nullopt;

            // TODO: move this somewhere else? maybe hook WBP_UI_HUD_Overlay_Right_C:OnInitialized?
            auto WBP_UI_HUD_Overlay_Right_C = Unreal::UObjectGlobals::FindObject(
                STR("WBP_UI_HUD_Overlay_Right_C"),
                nullptr,
                Unreal::EObjectFlags::RF_NoFlags,
                Unreal::EObjectFlags::RF_ClassDefaultObject | Unreal::EObjectFlags::RF_WasLoaded | Unreal::EObjectFlags::RF_LoadCompleted);
            if (WBP_UI_HUD_Overlay_Right_C != nullptr)
            {
                auto Text_Difficulty = *WBP_UI_HUD_Overlay_Right_C->GetValuePtrByPropertyNameInChain<Unreal::UObject *>(STR("Text_Difficulty"));
                if (Text_Difficulty != nullptr)
                {
                    auto Text = Text_Difficulty->GetValuePtrByPropertyName<Unreal::FText>(STR("Text"));
                    if (Text != nullptr)
                        difficulty_name = Text->ToString();
                }

                auto Text_LevelName = *WBP_UI_HUD_Overlay_Right_C->GetValuePtrByPropertyNameInChain<Unreal::UObject *>(STR("Text_LevelName"));
                if (Text_LevelName != nullptr)
                {
                    auto Text = Text_LevelName->GetValuePtrByPropertyName<Unreal::FText>(STR("Text"));
                    if (Text != nullptr)
                        heist_name = Text->ToString();
                }
            }

            auto PD3HUDPartyContainerWidget = Unreal::UObjectGlobals::FindFirstOf(STR("PD3HUDPartyContainerWidget"));
            if (PD3HUDPartyContainerWidget != nullptr)
            {
                auto Panel_PartyPlayerWidgetContainer = *PD3HUDPartyContainerWidget->GetValuePtrByPropertyNameInChain<Unreal::UObject *>(STR("Panel_PartyPlayerWidgetContainer"));
                if (Panel_PartyPlayerWidgetContainer != nullptr)
                {
                    auto Slots = Panel_PartyPlayerWidgetContainer->GetValuePtrByPropertyNameInChain<Unreal::TArray<Unreal::UObject *>>(STR("Slots"));
                    if (Slots != nullptr)
                        partySize = Slots->Num() + 1;
                }
            }

            discordPresence.state = "In Heist";
            if (heist_name.has_value() && difficulty_name.has_value())
            {
                auto formated = std::format(STR("{} | {}"), heist_name.value(), difficulty_name.value());
                discordPresence.details = WCharToChar(formated.c_str());
            }
            discordPresence.partySize = partySize;
            discordPresence.partyMax = 4; // hardcoded for now

            // TODO: get heist ID for largeImageKey
            // TODO: get heister name for smallImageKey

            break;
        }

        discordPresence.startTimestamp = startTimestamp;
        discordPresence.largeImageKey = "pd3"; // TODO: add heist images

#ifdef WITH_JOIN
        discordPresence.partyPrivacy = DISCORD_PARTY_PUBLIC; // TODO: private if SoloMode or ESBZOnlineJoinType::Private ESBZOnlineJoinType::FriendsOnly ESBZOnlineJoinType::InviteOnly

        // its possible that SBZPartyManager is already set if Status::InMenu
        if (SBZPartyManager == nullptr)
            SBZPartyManager = Unreal::UObjectGlobals::FindFirstOf(STR("SBZPartyManager"));

        if (SBZPartyManager != nullptr)
        {
            auto PartyId = SBZPartyManager->GetValuePtrByPropertyNameInChain<Unreal::FString>(STR("PartyId"));
            if (PartyId != nullptr)
                discordPresence.partyId = WCharToChar(PartyId->GetCharArray());

            auto PartyCode = SBZPartyManager->GetValuePtrByPropertyNameInChain<Unreal::FString>(STR("PartyCode"));
            if (PartyCode != nullptr)
                discordPresence.joinSecret = WCharToChar(PartyCode->GetCharArray());
        }
#endif

        Discord_UpdatePresence(&discordPresence);

#ifdef DISCORD_DISABLE_IO_THREAD
        Discord_UpdateConnection();
#endif
        Discord_RunCallbacks();
    }
};

#define MOD_EXPORT __declspec(dllexport)
extern "C"
{
    MOD_EXPORT CppUserModBase *start_mod() { return new DiscordMod(); }
    MOD_EXPORT void uninstall_mod(CppUserModBase *mod) { delete mod; }
}
