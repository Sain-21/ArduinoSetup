#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>

std::string getBrandName(std::string vid) 
{
    std::transform(vid.begin(), vid.end(), vid.begin(), ::toupper);
    if (vid == "046D") return "Logitech";
    if (vid == "1532") return "Razer";
    if (vid == "1B1C") return "Corsair";
    if (vid == "1038") return "SteelSeries";
    if (vid == "045E") return "Microsoft";
    if (vid == "0C45") return "Sonix (Keyboard/Generic)";
    return "Unknown Brand";
}

static bool fileExists(const char* path) 
{
    std::ifstream f(path);
    return f.good();
}

static const char* SKETCH = R"===(
#include <usbhid.h>
#include <hidcomposite.h>
#include <usbhub.h>
#include <PluggableUSB.h>
#include <HID.h>

#ifndef HID_REPORT_TYPE_FEATURE
#define HID_REPORT_TYPE_FEATURE 3
#endif

#define RID_MOUSE    1
#define RID_VENDOR   2
#define FEATURE_LEN  8

#define CMD_SET_CONFIG 0x01
#define CMD_GET_INFO   0x02
#define CMD_PING       0x03
#define CMD_LED        0x04
#define CMD_GET_MAP    0x05
#define CMD_MOVE       0x06

#define FLAG_PASSTHROUGH 0x01
#define FLAG_INVERT_X    0x02
#define FLAG_INVERT_Y    0x04
#define FLAG_SWAP_LR     0x08

#define ST_SHIELD_OK   0x01
#define ST_MOUSE_CONN  0x02
#define ST_MAP_OK      0x04
#define ST_DESC_ERR    0x08

static const uint8_t PROGMEM reportDescriptor[] = {
  0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, RID_MOUSE,
  0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01,
  0x29, 0x08, 0x15, 0x00, 0x25, 0x01, 0x95, 0x08,
  0x75, 0x01, 0x81, 0x02, 0x05, 0x01, 0x09, 0x30,
  0x09, 0x31, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F,
  0x75, 0x10, 0x95, 0x02, 0x81, 0x06, 0x09, 0x38,
  0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01,
  0x81, 0x06, 0xC0, 0xC0,
  
  0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, RID_VENDOR,
  0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x09, 0x02,
  0x95, FEATURE_LEN, 0xB1, 0x02, 0x09, 0x03, 0x95, FEATURE_LEN,
  0x81, 0x02, 0xC0
};

class MouseFeatureHID_ : public PluggableUSBModule {
public:
  MouseFeatureHID_() : PluggableUSBModule(1, 1, epType) {
    epType[0] = EP_TYPE_INTERRUPT_IN;
    PluggableUSB().plug(this);
  }

  volatile bool newData = false;
  uint8_t rxBuffer[FEATURE_LEN];
  uint8_t txBuffer[FEATURE_LEN];

  bool readFeature(uint8_t* dst) {
    if (!newData) return false;
    noInterrupts();
    memcpy(dst, (const void*)rxBuffer, FEATURE_LEN);
    newData = false;
    interrupts();
    return true;
  }
  void setFeatureResponse(const uint8_t* src) {
    noInterrupts();
    memcpy(txBuffer, src, FEATURE_LEN);
    interrupts();
  }

  void sendMouse(uint8_t buttons, int16_t x, int16_t y, int8_t wheel) {
    uint8_t r[7];
    r[0] = RID_MOUSE;
    r[1] = buttons;
    r[2] = (uint8_t)(x & 0xFF); r[3] = (uint8_t)((x >> 8) & 0xFF);
    r[4] = (uint8_t)(y & 0xFF); r[5] = (uint8_t)((y >> 8) & 0xFF);
    r[6] = (uint8_t)wheel;
    USB_Send(pluggedEndpoint | TRANSFER_RELEASE, r, sizeof(r));
  }

protected:
  int getInterface(uint8_t* interfaceCount) {
    *interfaceCount += 1;
    HIDDescriptor desc = {
      D_INTERFACE(pluggedInterface, 1, USB_DEVICE_CLASS_HUMAN_INTERFACE, 0, 0),
      D_HIDREPORT(sizeof(reportDescriptor)),
      D_ENDPOINT(USB_ENDPOINT_IN(pluggedEndpoint), USB_ENDPOINT_TYPE_INTERRUPT, USB_EP_SIZE, 0x01)
    };
    return USB_SendControl(0, &desc, sizeof(desc));
  }

  int getDescriptor(USBSetup& setup) {
    if (setup.bmRequestType != REQUEST_DEVICETOHOST_STANDARD_INTERFACE) return 0;
    if (setup.wValueH != HID_REPORT_DESCRIPTOR_TYPE) return 0;
    if (setup.wIndex  != pluggedInterface) return 0;
    return USB_SendControl(TRANSFER_PGM, reportDescriptor, sizeof(reportDescriptor));
  }

  bool setup(USBSetup& setup) {
    if (pluggedInterface != setup.wIndex) return false;
    const uint8_t request      = setup.bRequest;
    const uint8_t requestType = setup.bmRequestType;

    if (requestType == REQUEST_DEVICETOHOST_CLASS_INTERFACE) {
      if (request == HID_GET_REPORT) {
        if (setup.wValueH == HID_REPORT_TYPE_FEATURE && setup.wValueL == RID_VENDOR) {
          uint8_t out[1 + FEATURE_LEN];
          out[0] = RID_VENDOR;
          memcpy(&out[1], txBuffer, FEATURE_LEN);
          USB_SendControl(0, out, sizeof(out));
        }
        return true;
      }
      if (request == HID_GET_PROTOCOL) { USB_SendControl(0, &protocol, 1); return true; }
      if (request == HID_GET_IDLE)     { USB_SendControl(0, &idle, 1);     return true; }
    }

    if (requestType == REQUEST_HOSTTODEVICE_CLASS_INTERFACE) {
      if (request == HID_SET_PROTOCOL) { protocol = setup.wValueL; return true; }
      if (request == HID_SET_IDLE)     { idle     = setup.wValueL; return true; }
      if (request == HID_SET_REPORT) {
        if (setup.wValueH == HID_REPORT_TYPE_FEATURE && setup.wValueL == RID_VENDOR) {
          uint8_t in[1 + FEATURE_LEN];
          uint8_t len = setup.wLength;
          if (len == 0 || len > sizeof(in)) return false;
          USB_RecvControl(in, len);
          const uint8_t* data = (len == 1 + FEATURE_LEN) ? &in[1] : &in[0];
          memcpy((void*)rxBuffer, data, FEATURE_LEN);
          newData = true;
          return true;
        }
      }
    }
    return false;
  }
  uint8_t getShortName(char* name) { name[0]='M'; name[1]='P'; name[2]='T'; return 3; }
private:
  uint8_t epType[1];
  uint8_t protocol = 1;
  uint8_t idle     = 1;
};

MouseFeatureHID_ MouseHID;

struct Config {
  uint8_t flags = FLAG_PASSTHROUGH;
  uint8_t scale = 100;
};
static Config  cfg;
static uint8_t lastCmd = 0, lastArg = 0;
static uint8_t status  = 0;
static uint8_t rptCounter = 0;
static uint8_t lastLen = 0, lastRaw[4] = {0, 0, 0, 0};
static uint8_t currentButtons = 0;

struct Field { uint16_t bit; uint8_t size; };
struct MouseMap {
  uint8_t epAddr = 0;
  bool    hasId  = false;
  uint8_t rptId  = 0;
  bool    valid  = false;
  Field   x{0, 0}, y{0, 0}, wheel{0, 0}, btn{0, 0};
};

class MouseDescParser : public USBReadParser {
public:
  explicit MouseDescParser(MouseMap* map) : m(map) {}
  void Parse(const uint16_t len, const uint8_t* pbuf, const uint16_t& offset) override {
    (void)offset;
    for (uint16_t i = 0; i < len; i++) feed(pbuf[i]);
  }
  void finish() {
    m->valid = (m->x.size != 0 && m->y.size != 0);
    if (m->btn.size && btnId != m->rptId) m->btn.size = 0;
    if (m->wheel.size && wheelId != m->rptId) m->wheel.size = 0;
  }
private:
  MouseMap* m;
  uint8_t  prefix = 0, need = 0, have = 0, skip = 0;
  bool     longItem = false;
  uint32_t val = 0;
  uint16_t usagePage = 0;
  uint8_t  rptSize = 0, rptCount = 0, curId = 0;
  struct { uint8_t id; uint16_t bits; } tbl[8];
  uint8_t  tblN = 0;
  uint32_t usages[8];
  uint8_t  nUsages = 0;
  uint16_t usageMin = 0, usageMax = 0;
  bool     haveRange = false;
  uint8_t  btnId = 0, wheelId = 0;
  uint16_t& bitsFor(uint8_t id) {
    for (uint8_t i = 0; i < tblN; i++) if (tbl[i].id == id) return tbl[i].bits;
    if (tblN < 8) { tbl[tblN].id = id; tbl[tblN].bits = 0; return tbl[tblN++].bits; }
    return tbl[7].bits;
  }
  void clearLocals() { nUsages = 0; haveRange = false; usageMin = usageMax = 0; }
  void feed(uint8_t b) {
    if (skip)     { skip--; return; }
    if (longItem) { skip = b + 1; longItem = false; return; }
    if (need == 0 && have == 0) {
      prefix = b;
      if (prefix == 0xFE) { longItem = true; return; }
      need = prefix & 0x03; if (need == 3) need = 4;
      val = 0;
      if (need == 0) item();
      return;
    }
    val |= ((uint32_t)b) << (8 * have);
    if (++have == need) { item(); need = have = 0; }
  }
  void item() {
    switch (prefix & 0xFC) {
      case 0x04: usagePage = (uint16_t)val; break;
      case 0x74: rptSize   = (uint8_t)val;  break;
      case 0x94: rptCount  = (uint8_t)val;  break;
      case 0x84: curId = (uint8_t)val; m->hasId = true; break;
      case 0x08:
        if (nUsages < 8) usages[nUsages++] = (need == 4) ? val : (((uint32_t)usagePage << 16) | (val & 0xFFFF));
        break;
      case 0x18: usageMin = (uint16_t)val; haveRange = true; break;
      case 0x28: usageMax = (uint16_t)val; haveRange = true; break;
      case 0x80: input(); clearLocals(); break;
      case 0x90: case 0xB0: case 0xA0: case 0xC0: clearLocals(); break;
      default: break;
    }
  }
  void input() {
    uint16_t& bits = bitsFor(curId);
    const bool isConst = val & 0x01, isVar = val & 0x02;
    if (!isConst && isVar) {
      for (uint8_t i = 0; i < rptCount; i++) {
        uint32_t u = 0;
        if (nUsages)        u = usages[i < nUsages ? i : nUsages - 1];
        else if (haveRange) u = ((uint32_t)usagePage << 16) | (uint16_t)(usageMin + i);
        const uint16_t page = (uint16_t)(u >> 16), usage = (uint16_t)u;
        const uint16_t fieldBit = bits + (uint16_t)i * rptSize;
        if (page == 0x09 && rptSize == 1) {
          if (m->btn.size == 0) { m->btn.bit = fieldBit; btnId = curId; }
          if (btnId == curId && m->btn.size < 8) m->btn.size++;
        } else if (page == 0x01) {
          if (usage == 0x30 && !m->x.size)     { m->x = {fieldBit, rptSize}; m->rptId = curId; }
          if (usage == 0x31 && !m->y.size)     { m->y = {fieldBit, rptSize}; }
          if (usage == 0x38 && !m->wheel.size) { m->wheel = {fieldBit, rptSize}; wheelId = curId; }
        }
      }
    }
    bits += (uint16_t)rptSize * rptCount;
  }
};

static int32_t getBits(const uint8_t* d, uint8_t len, const Field& f, bool isSigned) {
  if (f.size == 0 || ((f.bit + f.size + 7) >> 3) > len) return 0;
  uint32_t v = 0;
  for (uint8_t i = 0; i < f.size && i < 32; i++) {
    const uint16_t b = f.bit + i;
    if (d[b >> 3] & (1 << (b & 7))) v |= (1UL << i);
  }
  if (isSigned && f.size < 32 && (v & (1UL << (f.size - 1)))) v |= ~((1UL << f.size) - 1);
  return (int32_t)v;
}

static void publishState(uint16_t vid, uint16_t pid);

USB     Usb;
USBHub  Hub(&Usb);

class MouseHost : public HIDComposite {
public:
  MouseHost(USB* p) : HIDComposite(p) {}
  uint16_t vid() const { return VID; }
  uint16_t pid() const { return PID; }
  bool connected = false;
  MouseMap maps[maxHidInterfaces];
  const MouseMap* activeMap() const {
    for (uint8_t i = 0; i < maxHidInterfaces; i++) if (maps[i].valid) return &maps[i];
    return nullptr;
  }
  bool SelectInterface(uint8_t iface, uint8_t proto) override { return true; }
  uint8_t Release() override {
    connected = false;
    status &= ~(ST_MOUSE_CONN | ST_MAP_OK | ST_DESC_ERR);
    for (uint8_t i = 0; i < maxHidInterfaces; i++) maps[i] = MouseMap();
    publishState(0, 0);
    return HIDComposite::Release();
  }
protected:
  uint8_t OnInitSuccessful() override {
    connected = true;
    status |= ST_MOUSE_CONN;
    for (uint8_t i = 0; i < bNumIface && i < maxHidInterfaces; i++) {
      const uint8_t idx = hidInterfaces[i].epIndex[epInterruptInIndex];
      maps[i] = MouseMap();
      if (idx == 0) continue;
      maps[i].epAddr = epInfo[idx].epAddr;
      MouseDescParser parser(&maps[i]);
      uint8_t buf[64];
      const uint8_t rc = pUsb->ctrlReq(bAddress, 0, bmREQ_HID_REPORT, USB_REQUEST_GET_DESCRIPTOR, 0x00,
                                       HID_DESCRIPTOR_REPORT, hidInterfaces[i].bmInterface,
                                       512, sizeof(buf), buf, &parser);
      if (rc) status |= ST_DESC_ERR;
      parser.finish();
      if (maps[i].valid) status |= ST_MAP_OK;
    }
    publishState(VID, PID);
    return 0;
  }
  void ParseHIDData(USBHID* hid, uint8_t ep, bool is_rpt_id, uint8_t len, uint8_t* buf) override {
    if (len == 0) return;
    rptCounter++;
    lastLen = len;
    for (uint8_t i = 0; i < 4; i++) lastRaw[i] = (i < len) ? buf[i] : 0;

    uint8_t btn = 0; int16_t x = 0, y = 0; int8_t wheel = 0;

    const MouseMap* map = nullptr;
    for (uint8_t i = 0; i < maxHidInterfaces; i++)
      if (maps[i].valid && maps[i].epAddr == ep) { map = &maps[i]; break; }

    if (map) {
      const uint8_t* d = buf;
      if (map->hasId) {
        if (buf[0] != map->rptId) return;
        d = buf + 1; len--;
      }
      int32_t rx = getBits(d, len, map->x, true);
      int32_t ry = getBits(d, len, map->y, true);
      int32_t rw = getBits(d, len, map->wheel, true);
      x = (int16_t)constrain(rx, -32767L, 32767L);
      y = (int16_t)constrain(ry, -32767L, 32767L);
      wheel = (int8_t)constrain(rw, -127L, 127L);
      btn = (uint8_t)getBits(d, len, map->btn, false);
    } else {
      if (len < 3) return;
      btn = buf[0]; x = (int8_t)buf[1]; y = (int8_t)buf[2]; if (len >= 4) wheel = (int8_t)buf[3];
    }

    currentButtons = btn; 

    if (!(cfg.flags & FLAG_PASSTHROUGH)) return;

    if (cfg.scale != 100) {
      x = (int16_t)(((int32_t)x * cfg.scale) / 100);
      y = (int16_t)(((int32_t)y * cfg.scale) / 100);
    }
    if (cfg.flags & FLAG_INVERT_X) x = -x;
    if (cfg.flags & FLAG_INVERT_Y) y = -y;
    if (cfg.flags & FLAG_SWAP_LR) {
      uint8_t l = btn & 1, r = (btn >> 1) & 1;
      btn = (btn & ~0x03) | (r) | (l << 1);
    }
    MouseHID.sendMouse(btn, x, y, wheel);
  }
};

MouseHost Mouse(&Usb);

static void publishState(uint16_t vid, uint16_t pid) {
  uint8_t r[FEATURE_LEN];
  if (lastCmd == CMD_GET_INFO) {
    r[0] = CMD_GET_INFO; r[1] = status; r[2] = rptCounter; r[3] = lastLen;
    memcpy(&r[4], lastRaw, 4);
  } else if (lastCmd == CMD_GET_MAP) {
    const MouseMap* m = Mouse.activeMap();
    r[0] = CMD_GET_MAP;
    if (m) {
      r[1] = (uint8_t)((m->hasId ? 0x80 : 0) | (m->rptId & 0x7F));
      r[2] = (uint8_t)m->x.bit;   r[3] = m->x.size;
      r[4] = (uint8_t)m->y.bit;   r[5] = m->y.size;
      r[6] = (uint8_t)m->btn.bit; r[7] = m->btn.size;
    } else {
      memset(&r[1], 0, 7);
    }
  } else {
    r[0] = lastCmd;
    r[1] = lastArg;
    r[2] = (uint8_t)(vid & 0xFF); r[3] = (uint8_t)(vid >> 8);
    r[4] = (uint8_t)(pid & 0xFF); r[5] = (uint8_t)(pid >> 8);
    r[6] = cfg.flags;
    r[7] = cfg.scale;
  }
  MouseHID.setFeatureResponse(r);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  publishState(0, 0);
  if (Usb.Init() == -1) {
    while (1) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(100);
      uint8_t f[FEATURE_LEN];
      if (MouseHID.readFeature(f)) { lastCmd = f[0]; lastArg = f[1]; publishState(0, 0); }
    }
  }
  status |= ST_SHIELD_OK;
  delay(200);
}

void loop() {
  Usb.Task();

  uint8_t f[FEATURE_LEN];
  if (MouseHID.readFeature(f)) { 
    lastCmd = f[0];
    lastArg = f[1];
    switch (f[0]) {
      case CMD_SET_CONFIG:
        cfg.flags = f[1];
        if (f[2] != 0) cfg.scale = f[2];
        break;
      case CMD_LED:
        digitalWrite(LED_BUILTIN, f[1] ? HIGH : LOW);
        break;
      case CMD_MOVE:
        MouseHID.sendMouse(currentButtons, (int8_t)f[1], (int8_t)f[2], 0);
        break;
      case CMD_GET_INFO:
      case CMD_GET_MAP:
      case CMD_PING:
      default:
        break;
    }
    publishState(Mouse.connected ? Mouse.vid() : 0, Mouse.connected ? Mouse.pid() : 0);
  }
}
)===";

class ArduinoFlasherUI 
{
public:
    enum class State 
    {
        Idle,
        Scanning,
        WaitingSelection,
        Flashing,
        Success,
        Error
    };

    ArduinoFlasherUI() 
    {
        AddLog("[Info] Welcome! Click 'Start Setup' to scan for mice.");
    }

    void Render(bool* p_open) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

        if (!ImGui::Begin("Arduino Spoofer & Flasher | Creator: Sa1n", p_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::End();
            ImGui::PopStyleVar(2);
            return;
        }

        ImGui::Text("Log Output:");
        ImGui::BeginChild("LogRegion", ImVec2(0, 260), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        {
            //log colors
            std::lock_guard<std::mutex> lock(logMutex);
            for (const auto& log : logs) {
                ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

                if (log.find("[-] ERROR") != std::string::npos || log.find("[-]") != std::string::npos || log.find("[!]") != std::string::npos) {
                    color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                }
                else if (log.find("[Info]") != std::string::npos || log.find("[*]") != std::string::npos) {
                    color = ImVec4(1.0f, 0.9f, 0.2f, 1.0f);
                }
                else if (log.find("[+]") != std::string::npos || log.find("[+++]") != std::string::npos) {
                    color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
                }

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextWrapped("%s", log.c_str());
                ImGui::PopStyleColor();
            }
        }

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
        ImGui::Separator();

        if (currentState == State::WaitingSelection) 
        {
            ImGui::Text("Select your REAL mouse:");
            if (ImGui::BeginCombo("##mouseSelect", devices[selectedDeviceIdx].first.c_str())) 
            {
                for (int i = 0; i < (int)devices.size(); ++i) 
                {
                    bool isSelected = (selectedDeviceIdx == i);
                    std::string label = devices[i].first + " (VID: " + devices[i].second.first + ")";
                    if (ImGui::Selectable(label.c_str(), isSelected)) 
                    {
                        selectedDeviceIdx = i;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();

            if (ImGui::Button("Continue")) 
            {
                StartFlashingThread();
            }
        }

        ImGui::Spacing();

        if (currentState == State::Idle || currentState == State::Error || currentState == State::Success) 
        {
            std::string btnText = "Start Setup";
            if (currentState == State::Error) btnText = "Retry";
            if (currentState == State::Success) btnText = "Exit";

            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 120) * 0.5f);
            if (ImGui::Button(btnText.c_str(), ImVec2(120, 30))) 
            {
                if (currentState == State::Success) 
                {
                    *p_open = false;
                }
                else 
                {
                    StartScanningThread();
                }
            }
        }
        else if (currentState == State::Scanning || currentState == State::Flashing) 
        {
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 160) * 0.5f);
            ImGui::BeginDisabled();
            ImGui::Button("Please wait...", ImVec2(160, 30));
            ImGui::EndDisabled();
        }

        ImGui::SetCursorPos(ImVec2(10, ImGui::GetWindowHeight() - 35));
        if (ImGui::Button("Help / Report Bug")) 
        {
            AddLog("[Info] Help clicked. Troubleshooting instructions should be followed if errors occur.");
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

private:
    State currentState = State::Idle;
    std::vector<std::string> logs;
    std::mutex logMutex;

    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> devices;
    int selectedDeviceIdx = 0;

    void AddLog(const std::string& msg) {
        std::lock_guard<std::mutex> lock(logMutex);
        logs.push_back(msg);
    }

    //0 console
    int exec_silent(const std::string& cmd, std::string& out_output) 
    {
        HANDLE hReadPipe, hWritePipe;
        SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return -1;
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si = { sizeof(STARTUPINFOA) };
        si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;

        PROCESS_INFORMATION pi = { 0 };
        std::string full_cmd = "cmd.exe /c " + cmd;
        std::vector<char> cmd_buffer(full_cmd.begin(), full_cmd.end());
        cmd_buffer.push_back('\0');

        if (!CreateProcessA(NULL, cmd_buffer.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) 
        {
            CloseHandle(hWritePipe);
            CloseHandle(hReadPipe);
            return -1;
        }
        CloseHandle(hWritePipe);

        char buffer[256];
        DWORD bytesRead;
        out_output.clear();
        while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) 
        {
            buffer[bytesRead] = '\0';
            out_output += buffer;
        }

        CloseHandle(hReadPipe);
        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        return (int)exitCode;
    }

    std::string run_logged(const std::string& cmd) 
    {
        std::string output;
        exec_silent(cmd, output);
        return output;
    }

    void HandleError(const std::string& msg) 
    {
        AddLog("[-] ERROR: " + msg);
        AddLog("\n--- TROUBLESHOOTING ---");
        AddLog("1. Ensure the Arduino is properly connected to the PC.");
        AddLog("2. Double-tap the RESET button on the Arduino Board before clicking Retry.");
        AddLog("3. If arduino-cli is missing, ensure you have an active internet connection.");
        AddLog("-----------------------\n");
        currentState = State::Error;
    }

    void StartScanningThread() 
    {
        currentState = State::Scanning;
        logs.clear();
        AddLog("[*] Scanning for connected mice...");

        std::thread([this]() 
            {
            std::string psCommand = "powershell -command \"Get-PnpDevice -Class Mouse -Status OK | ForEach-Object { $_.InstanceId }\"";
            std::string output = run_logged(psCommand.c_str());

            std::istringstream stream(output);
            std::string line;
            std::regex hwidRegex("VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})");
            std::smatch match;

            devices.clear();
            while (std::getline(stream, line)) 
            {
                if (line.empty()) continue;
                if (std::regex_search(line, match, hwidRegex)) 
                {
                    std::string vid = match[1].str();
                    std::string pid = match[2].str();
                    if (vid != "2341") 
                    {
                        devices.push_back({ getBrandName(vid) + " Mouse", {vid, pid} });
                    }
                }
            }

            if (devices.empty()) 
            {
                HandleError("No compatible mouse found on the system.");
                return;
            }

            AddLog("[+] Mice found! Please select your actual device from the dropdown.");
            selectedDeviceIdx = 0;
            currentState = State::WaitingSelection;
            }).detach();
    }

    void StartFlashingThread() 
    {
        currentState = State::Flashing;

        std::string vid = devices[selectedDeviceIdx].second.first;
        std::string pid = devices[selectedDeviceIdx].second.second;
        AddLog("[+] Starting Spoofing with VID: " + vid + " & PID: " + pid);

        std::thread([this, vid, pid]() 
            {
            std::string dummy_out;

            // Dimiourgia tou fakelou includes an den uparxei
            exec_silent("mkdir includes >nul 2>nul", dummy_out);

            AddLog("[*] Checking arduino-cli...");
            if (!fileExists("includes\\arduino-cli.exe")) {
                AddLog("[!] arduino-cli not found. Downloading to includes folder...");
                // Katevasma kai extract kateutheian mesa sto includes
                int rc = exec_silent("powershell -Command \"Invoke-WebRequest -Uri 'https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip' -OutFile 'includes\\cli.zip'; Expand-Archive includes\\cli.zip -DestinationPath includes -Force; Remove-Item includes\\cli.zip\"", dummy_out);
                if (rc != 0 || !fileExists("includes\\arduino-cli.exe")) {
                    HandleError("Failed to download arduino-cli.");
                    return;
                }
            }

            AddLog("[*] Creating Sketch files...");
            exec_silent("mkdir includes\\MousePassThrough >nul 2>nul", dummy_out);
            {
                // To arxeio swzetai pleon mesa sto includes/MousePassThrough
                std::ofstream ino("includes/MousePassThrough/MousePassThrough.ino");
                ino << SKETCH;
            }

            AddLog("[*] Updating libraries (This might take a moment)...");
            // Oles oi entoles trexoun pleon apo to includes\arduino-cli.exe
            exec_silent("includes\\arduino-cli.exe core update-index >nul 2>nul", dummy_out);
            exec_silent("includes\\arduino-cli.exe core install arduino:avr >nul 2>nul", dummy_out);
            exec_silent("includes\\arduino-cli.exe lib install \"USB Host Shield Library 2.0\" >nul 2>nul", dummy_out);

            AddLog("[*] Searching for Arduino Leonardo...");
            std::string boardList = run_logged("includes\\arduino-cli.exe board list");
            std::regex comRegex("(COM[0-9]+).*(Leonardo|2341:8036)");
            std::smatch match;
            if (!std::regex_search(boardList, match, comRegex)) {
                HandleError("Arduino Leonardo not found. (Hint: Double-tap RESET on the board)");
                return;
            }
            std::string comPort = match[1].str();
            AddLog("[+] Leonardo found on port " + comPort);

            AddLog("[*] Compiling (Applying Spoof & Disabling COM Port)...");
            std::string compileCmd = "includes\\arduino-cli.exe compile --fqbn arduino:avr:leonardo "
                "--build-property \"build.vid=0x" + vid + "\" "
                "--build-property \"build.pid=0x" + pid + "\" "
                "--build-property \"build.extra_flags=-DCDC_DISABLED\" "
                "includes\\MousePassThrough";

            if (exec_silent(compileCmd, dummy_out) != 0) {
                HandleError("Compilation failed.");
                return;
            }

            AddLog("[*] Compile OK! Uploading to " + comPort + "...");
            std::string uploadCmd = "includes\\arduino-cli.exe upload -p " + comPort + " --fqbn arduino:avr:leonardo includes\\MousePassThrough";

            if (exec_silent(uploadCmd, dummy_out) != 0) {
                HandleError("Upload failed. Double-tap RESET on the board and try again.");
                return;
            }

            AddLog("[+++] SUCCESS! Leonardo is now SPOOFED (" + vid + ":" + pid + ") and HID-Only (No COM).");
            AddLog("[Info] Plug the mouse into the Host Shield and you are ready.");
            currentState = State::Success;

         }).detach();
    }
};


static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int main(int, char**)
{
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"Insa1n-Spoofer", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Insa1n-Spoofer", WS_POPUP, 100, 100, 550, 420, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();

    //fonts
    if (fileExists("C:\\Windows\\Fonts\\segoeuib.ttf")) 
    {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 17.0f);
    }
    else if (fileExists("C:\\Windows\\Fonts\\arialbd.ttf")) 
    {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arialbd.ttf", 16.0f);
    }

    //red theme
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.5f, 0.1f, 0.1f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.7f, 0.1f, 0.1f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.6f, 0.1f, 0.1f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.6f, 0.1f, 0.1f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.2f, 0.05f, 0.05f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.3f, 0.1f, 0.1f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.4f, 0.15f, 0.15f, 1.0f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.8f, 0.2f, 0.2f, 0.5f);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ArduinoFlasherUI flasherUI;
    bool showMainWindow = true;
    ImVec4 clear_color = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);

    //main loop
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (showMainWindow) 
        {
            flasherUI.Render(&showMainWindow);
        }

        if (!showMainWindow) 
        {
            done = true;
        }

        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

//helpers
bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_NCHITTEST:
    {
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        ScreenToClient(hWnd, &pt);
        RECT rc;
        GetClientRect(hWnd, &rc);

        if (pt.y < 30 && pt.x < (rc.right - 45))
            return HTCAPTION;

        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}