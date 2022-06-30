#pragma once

#include <string>
#include <libnotify/notify.h>

#define NOTIF_TIMEOUT 5000

inline void notifInit(const std::string& appName)
{
    notify_init(appName.c_str());
}

inline void notifShow(const std::string& title, const std::string& msg)
{
    NotifyNotification* notif = notify_notification_new(title.c_str(), msg.c_str(), nullptr);
    notify_notification_set_timeout(notif, NOTIF_TIMEOUT);
    notify_notification_show(notif, nullptr);
}

inline void notifUninit()
{
    notify_uninit();
}
