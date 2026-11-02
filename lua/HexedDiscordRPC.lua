script_name("Hexed Discord RPC")
script_author("HEXED Studio")

local ffi = require("ffi")
ffi.cdef[[
int  HexedRPC_Start(const char* appId, const char* dataDir);
void HexedRPC_Stop(void);
void HexedRPC_Login(void);
int  HexedRPC_Status(void);
void HexedRPC_SetAssets(const char* largeKey, const char* largeText);
void HexedRPC_SetServer(const char* hostname, const char* nick, const char* ip, int port);
void HexedRPC_ClearServer(void);
]]

local APP_ID    = "PASTE_YOUR_APPLICATION_ID_HERE"  -- string, not a number
local ART_KEY   = "samp"                            -- Art Asset name uploaded in the Developer Portal
local ART_TEXT  = "San Andreas Multiplayer"

local STATUS = { [-1]="error", [0]="stopped", [1]="login needed (/hxdrpc)", [2]="waiting for Discord", [3]="connecting", [4]="connected" }

local function loadLib()
  local tries = { "libHexedDiscordRPC.so", getWorkingDirectory() .. "/lib/libHexedDiscordRPC.so" }
  for _, p in ipairs(tries) do
    local ok, lib = pcall(ffi.load, p)
    if ok then return lib end
  end
end

function main()
  while not isSampAvailable() do wait(100) end
  local rpc = loadLib()
  if not rpc then sampAddChatMessage("[HexedRPC] could not load libHexedDiscordRPC.so", 0xFF5555) return end

  rpc.HexedRPC_Start(APP_ID, getWorkingDirectory())
  rpc.HexedRPC_SetAssets(ART_KEY, ART_TEXT)

  sampRegisterChatCommand("hxdrpc", function(arg)
    if arg == "status" then
      sampAddChatMessage("[HexedRPC] " .. (STATUS[rpc.HexedRPC_Status()] or "?"), 0x55FF55)
    else
      rpc.HexedRPC_Login()
      sampAddChatMessage("[HexedRPC] opening Discord to authorize...", 0x55FF55)
    end
  end)

  while true do
    wait(3000)
    local okName, name = pcall(sampGetCurrentServerName)
    local okAddr, ip, port = pcall(sampGetCurrentServerAddress)
    local okId, id = pcall(function() return select(2, sampGetPlayerIdByCharHandle(PLAYER_PED)) end)
    if okName and okAddr and okId and name and name ~= "" and ip then
      local nick = sampGetPlayerNickname(id) or "Unknown"
      rpc.HexedRPC_SetServer(name, nick, ip, port)
    else
      rpc.HexedRPC_ClearServer()
    end
  end
end

function onScriptTerminate(s)
  if s == thisScript() then
    local rpc = loadLib()
    if rpc then rpc.HexedRPC_Stop() end
  end
end
