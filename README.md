# HexedDiscordRPC (HEXED Studio)

Discord Rich Presence for SA-MP Nezuko using the Discord Social SDK.
Shows: "Playing <app name>" / server hostname / `nick • ip:port` / elapsed timer.

## 1. Build (GitHub, private repo)
1. Create a **private** repo and upload this whole folder (the SDK files in `third_party/` are Discord's, keep the repo private).
2. Actions -> "Build HexedDiscordRPC" -> Run workflow.
3. Download the `HexedDiscordRPC` artifact:
   - `lib/<abi>/libHexedDiscordRPC.so` and `lib/<abi>/libdiscord_partner_sdk.so` (armeabi-v7a + arm64-v8a)
   - `dex/classes*.dex` (the SDK's Java classes)

## 2. Discord Developer Portal
- App name = what shows after "Playing" (e.g. San Andreas Multiplayer).
- Rich Presence -> Art Assets: upload the icon with the key used in the Lua (`samp`).
- OAuth2 -> Redirects: `discord-YOUR_APP_ID:/authorize/callback`; enable Public Client.
- Social SDK -> Getting Started: enabled.

## 3. Patch the APK (apktool)
1. Copy both `.so` files into `lib/<abi>/` matching the ABI of your Nezuko build.
2. Copy `dex/classes*.dex` into the APK as extra dex files (rename to the next free `classesN.dex`).
3. AndroidManifest.xml, inside `<manifest>` (so Android 11+ can see the Discord app):
```xml
<queries>
  <intent><action android:name="android.support.customtabs.action.CustomTabsService"/></intent>
  <package android:name="com.discord"/>
</queries>
```
   and inside `<application>`:
```xml
<activity android:name="com.discord.socialsdk.AuthenticationActivity"
          android:theme="@android:style/Theme.Translucent.NoTitleBar"
          android:launchMode="singleTask"
          android:exported="true">
  <intent-filter>
    <action android:name="android.intent.action.VIEW"/>
    <category android:name="android.intent.category.DEFAULT"/>
    <category android:name="android.intent.category.BROWSABLE"/>
    <data android:scheme="discord-YOUR_APP_ID"/>
  </intent-filter>
</activity>
```
   Make sure `android.permission.INTERNET` is present.
4. In the smali of the main (launcher) activity's `onCreate`, right after `super.onCreate(...)`:
```smali
invoke-static {p0}, Lcom/discord/socialsdk/DiscordSocialSdkInit;->setEngineActivity(Landroid/app/Activity;)V
```
   Rebuild, align, sign.

## 4. Script
Put `libHexedDiscordRPC.so` where the APK's native libs live (step 3), edit `APP_ID` in
`lua/HexedDiscordRPC.lua`, put it in your moonloader folder. In game: `/hxdrpc` once to link
your Discord account (opens Discord), `/hxdrpc status` to check. The refresh token is saved
as `hexed_discord_token.txt` in the moonloader dir, so later launches reconnect silently.

## C API
HexedRPC_Start(appIdString, dataDir) / Stop / Login / Status / SetAssets / SetServer / ClearServer
Status: 0 stopped, 1 needs login, 2 waiting for Discord, 3 connecting, 4 ready, -1 error.
