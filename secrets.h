#pragma once

struct APEntry { const char* ssid; const char* pass; const char* name; };

static const APEntry WIFI_APS[] = {
    { "YourSSID1", "YourPass1", "Home"   },
    { "YourSSID2", "YourPass2", "Mobile" },
};
