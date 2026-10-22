#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>

#ifndef NATIVE_TEST
#include <DNSServer.h>
#include <WebServer.h>
#include <ImprovWiFiLibrary.h>
#endif

enum WifiState {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_MODE
};

class WifiManager {
public:
    WifiManager(const char* ssid, const char* password);
    void begin();
    void update();
    WifiState getState() const;
    String getIPAddress() const;
    int8_t getRSSI() const;
    void setCredentials(const String& ssid, const String& password);
    String getAPSSID();
    String getSSID() const;  // network currently being tried / connected to

private:
    void startAPMode();
    void handleRoot();
    void handleSave();
    void handleNotFound();
    void loadCredentials();
    void connectTo(int idx);
    void startConnectCycle();
    bool tryOtherNetwork();

    // Two networks: [0] = "ssid"/"pass" prefs (or the constructor default), [1] = "ssid2"/"pass2".
    // Both are tried before falling back to the captive portal.
    static constexpr int kNetworkCount = 2;
    String _defaultSsid;
    String _defaultPassword;
    String _ssids[kNetworkCount];
    String _passwords[kNetworkCount];
    int _netIdx = 0;
    int _netsTried = 0;
    WifiState _state;
    unsigned long _lastReconnectAttempt;
    unsigned long _connectionStartTime;
    const unsigned long _reconnectInterval = 10000; // 10 seconds
    const unsigned long _connectionTimeout = 30000; // 30 seconds
    const unsigned long _failStatusGraceMs = 2000;  // ignore a stale fail status right after WiFi.begin()
    String _cachedNetworksHTML;

#ifndef NATIVE_TEST
    DNSServer* _dnsServer = nullptr;
    WebServer* _webServer = nullptr;
    ImprovWiFi* _improv = nullptr;
    
    
    
    
#endif
};

#endif // WIFI_MANAGER_H
