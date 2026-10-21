#pragma once
#include "service_data.h"
#include <string>

namespace services {
struct Status {
    bool busy=false, connected=false, backupExists=false, updateAvailable=false;
    bool downloadReady=false, restartRequired=false, quitRequested=false;
    bool autoCheck=true, restoreReady=false, checkingUpdates=false, updateNotice=false, downloading=false;
    uint64_t downloadedBytes=0,totalBytes=0;
    std::string message, userCode, verificationUrl, backupDate, backupSummary;
    Release release;
};
void init(const std::string& dataRoot,const std::string& executable,const std::string& resources);
void shutdown();
Status status();
void checkUpdate(bool manual);
void pollStartup(uint64_t now);
bool takeUpdateNotice();
void downloadUpdate();
void applyUpdate();
void login();
void cancelLogin();
void logout();
void refreshBackup();
void uploadBackup();
void restoreBackup();
bool applyPendingRestore();
void setAutoCheck(bool enabled);
void requestQuit();
bool romfsWasReleased();
std::string resourcePath(const std::string& name);
}
