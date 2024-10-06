/*
An EPS32 program that displays a list of Wi-Fi networks in the terminal.

Can use with android Serial USB terminal such UsbTerminal
115200 8 N 1

1. Flash to any esp32 board with a hardware serial
2. Open terminal or connect to the mobile phone using OTA cable

VT-100 support must be autodetected

Released into the public domain.

Created by Denis, 2023. Thanks for https://github.com/LieBtrau
*/

#include <Arduino.h>
#include "WiFi.h"

#define SCANS_COUNT 50
//set 0 to auto detect
#define SCANS_MIN -90
//set -127 to auto detect
#define SCANS_MAX -30

//-----------------------------------------------------------------------------------
//Xterm
typedef enum{
        NORMAL=0,
        BOLD=1,
        UNDERLINED=4,
        BLINK=5,
        INVERSE=7,
    }CHARACTERTYPE;
typedef enum{
    BLACK=0,
    RED=1,
    GREEN=2,
    YELLOW=3,
    BLUE=4,
    MAGENTA=5,
    CYAN=6,
    WHITE=7,
    DEF=9
}COLOR;

class Xterm
{
public:
    Xterm(HardwareSerial * stream);
    bool init();
    bool deinit();
    void print(char);
    void print(uint8_t);
    void print(int);
    void print(unsigned int);
    void print(long);
    void print(unsigned long);
    template <typename... Args> size_t printf(const int row, const int col, const CHARACTERTYPE m,const char * format, Args... args){
        setCursorType(m);
        setCursorPos(row, col);
        return _stream->printf(format, args...);
    }
    void setCursorPos(int row, int col);
    void setCursorType(CHARACTERTYPE m);
    void setForegroundColor(COLOR c);
    void setBackgroundColor(COLOR c);
    bool getTerminalType(byte& terminalType);
    template<typename T> void print(int row, int col, T &t, CHARACTERTYPE m)
    {
        setCursorType(m);
        setCursorPos(row, col);
        _stream->print(t);          // text
    }
    void clear();
private:
    void setCharacterAttributes(byte m);
    HardwareSerial* _stream;
};

Xterm::Xterm(HardwareSerial* stream): _stream(stream){
}
bool Xterm::init()
{
    byte type;
    if(!getTerminalType(type))
    {
        return false;
    }
    _stream->print("\eSP F");  	// tell to use 7-bit control codes (will be echoed back)
    _stream->print("\e[?25l"); 	// hide cursor
    _stream->print("\e[?12l");	// disable cursor highlighting
    clear(); 	// clear entire screen
    return true;
}

bool Xterm::deinit()
{
    byte type;
    if(!getTerminalType(type))
    {
        return false;
    }
    
    _stream->print("\e[?25h"); 	// show cursor
    _stream->print("\e[?12h");	// enable cursor highlighting
    _stream->print("\eSP G");  	// set 8 bit codes
    return true;
}

//\e[?62;3c    //VT220 with ReGIS graphics (response from GTKTerm)
//\e[?1;2c     //VT100 with Advanced Video Option (response from minicom)
bool Xterm::getTerminalType(byte& terminalType)
{
    while(_stream->available()>0)_stream->read();       //clear input buffer
    _stream->print("\e[c");                             // request attributes from terminal
     String response=_stream->readStringUntil('c');
    if(response.length()==0 || response.charAt(0)!='\e' ||
            response.charAt(1)!='[' || response.charAt(2)!='?')
    {
        return false;
    }
    byte semicolonPos=response.indexOf(';');
    if(semicolonPos<0){
        return false;
    }
    response=response.substring(3,semicolonPos);
    terminalType=response.toInt();
    return true;
}

void Xterm::clear()
{
    _stream->print("\e[2J"); // clear screen
}

#include <cstdarg>

void Xterm::setCursorPos(int row, int col){
    _stream->print("\e[");  	// CSI (control sequence initializer)
    _stream->print(row);
    _stream->print(";");
    _stream->print(col);
    _stream->print("f");
}

void Xterm::setForegroundColor(COLOR c)
{
    if(c>=BLACK && c!=DEF){
        setCharacterAttributes(c+30);
    }
}

void Xterm::setBackgroundColor(COLOR c)
{
    if(c>=BLACK && c!=DEF){
        setCharacterAttributes(c+40);
    }

}

void Xterm::setCursorType(CHARACTERTYPE m){
    switch (m) // m = mode
    {
    case 1: // Bold
    case 4: // Underlined
    case 5: // Blink
    case 7: // Inverse
        setCharacterAttributes(m);
        break;
    default: // Normal
        setCharacterAttributes(0);

    }
}

void Xterm::setCharacterAttributes(byte m){
    String cmd="\e[";
    cmd+= String(m);
    cmd+='m';
    _stream->print(cmd);
}

#if ARDUINO_USB_CDC_ON_BOOT //Serial used for USB CDC
Xterm xterm=Xterm(&Serial0);
#else
Xterm xterm=Xterm(&Serial);
#endif
//----------------------------------------------------------------------------------
bool writeScreen();
bool writeScreen1(String ssid);
void drawMode0Xterm(int n);
void drawMode0(int n);

void drawMode1XtermFrameIfNeeded(bool rebuild);

void drawMode1Xterm(String ssid);
void drawMode1Xterm_old(String ssid);
void drawMode1(String ssid);

void writeMid(int row);
void writeBot(int row);

void checkInput();

bool useXterm = false;
int vmode = 0; //global visualization mode

struct datatype {
    String name;
    unsigned long first;
    unsigned long last;
    int count;
    int lastRSSI;
    int64_t sumRSSI;
    int channel;
    wifi_auth_mode_t encryptionType;
    int scan_count;
    int unique_count;

    //mesh counter:
    int counter_tag;
    int mesh_counter;
    int mesh_size;
};

#include <vector>

std::vector<datatype> data;
std::vector<int32_t> scans;
int32_t scans_min = 0;
int32_t scans_max = -127;

int rc=0;
String cmd = "";
String ssid = ""; //ssid to indicate
int channel = 0;
int scandelay = 1000;

void set_xterm(bool use=true) {
    if (use) {
        byte type;

        if(!xterm.getTerminalType(type) || (type<1)) {
            Serial.printf("WARNING: unknown terminal type. xterm functions disabled\n",type);
        } else {
            xterm.init();
            useXterm = true;
            //xterm.print(1,1,"Starting...",NORMAL); 
            if (ssid.isEmpty()) writeScreen();
            else writeScreen1(ssid);
        }
    } else {
        if (useXterm) {
            xterm.clear();
            xterm.deinit();
        }
        useXterm = false;
    }
}

void setup() {
  Serial.begin(115200);
  delay(5);//5ms


  set_xterm();
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
}

int current_tag = 0;
int scanNetworks() {
    int n = WiFi.scanNetworks();
    checkInput();

    current_tag++;

    //increase scan count for all networks
    for (auto &d : data) d.scan_count++;

    for (int i=0; i<n; i++) {
        bool found = false;
        for (auto &d : data) {// access by reference to avoid copying
            if (d.name == WiFi.SSID(i)) {
                found = true;
                d.last = millis();
                d.lastRSSI = WiFi.RSSI(i);
                d.sumRSSI += d.lastRSSI;
                d.count++;

                if (d.counter_tag==current_tag) d.mesh_counter++;
                else {
                    d.counter_tag = current_tag;
                    d.mesh_counter = 1;
                    d.unique_count++;
                }
                if (d.mesh_size<d.mesh_counter) d.mesh_size=d.mesh_counter;

                break;
            }
        }

        if (!found) data.push_back(datatype{
          name:WiFi.SSID(i).c_str(),  
          first:millis(),
          last:millis(),
          count:1,
          lastRSSI:WiFi.RSSI(i),
          sumRSSI:WiFi.RSSI(i),
          channel:WiFi.channel(i),
          encryptionType:WiFi.encryptionType(i),
          scan_count:1,
          unique_count:1,

          counter_tag:current_tag,
          mesh_counter:1,
          mesh_size:1
        });
    }
    return n;
}

void checkInput() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == 27) {
            cmd = "";
            ssid = "";  
            if (useXterm) writeScreen();
        } else if (c>='0' && c<='9') {
            cmd += c;
        } else if (c == 13) {
            if (cmd.length()>0 && cmd.toInt()>0 && cmd.toInt()<=data.size()) {
                ssid = data[cmd.toInt()-1].name;
                channel = data[cmd.toInt()-1].channel; 
                scans.clear(); scans_min = SCANS_MIN; scans_max = SCANS_MAX;
                if (useXterm) writeScreen1(ssid);
                else Serial.printf("Selected %s\n",ssid.c_str());
            } else {
                ssid = "";
                if (useXterm) writeScreen();
            }
            cmd = "";
        } else if (c == '-') {
            scandelay+=100;
            cmd = "";
        } else if (c == '+') {
            scandelay-=100;
            if (scandelay<100) scandelay = 100;
            cmd = "";
        } else if (c == '/') {
            set_xterm(!useXterm);
        } else if (c == '*') {
            vmode = (vmode + 1) % 2; //we have 2 modes now
        } else if (c == 'r') {
            data.clear();
        } else {
            cmd = "";
        }
    }
}

void loop() {
  do {
    checkInput();

    int n;
    if (ssid.isEmpty()) n=scanNetworks();
    else {
        //check selected network RSSI
        n = WiFi.scanNetworks(false,false,false,300U,channel, ssid.c_str());
        checkInput();

        if (n>0) {
            /*
            bool found = false;
            for (int i=0; i<n; i++) {
                if (WiFi.SSID(i) == ssid) {
                    scans.push_back(WiFi.RSSI(i));
                    found = true;
                    break;
                }
            }
            if (!found) scans.push_back(0);
            */
            scans.push_back(WiFi.RSSI(0));
            if (scans_min>scans.back()) scans_min = scans.back();
            if (scans_max<scans.back()) scans_max = scans.back();
        } else {
            scans.push_back(0);
        }
        if (scans.size()>SCANS_COUNT) scans.erase(scans.begin());
    }


    if (ssid.isEmpty()) {
        if (useXterm) {
            drawMode0Xterm(n);
        } else {
            drawMode0(n);
        }
    } else {
        if (useXterm) {
            if (vmode==0) drawMode1Xterm(ssid);
            else drawMode1Xterm_old(ssid);
        } else {
            drawMode1(ssid);
        }
    }
    
    delay(scandelay);
  } while (true);
}


void writeMid(int row) {
    xterm.print(row,1,"║    ║                                ║       ║       ║           ║",NORMAL); 
}
void writeBot(int row) {
    xterm.print(row,1,"╚════╩════════════════════════════════╩═══════╩═══════╩═══════════╝",NORMAL); 
}
bool writeScreen() {
  xterm.clear();

                 //00000000011111111112222222222333333333344444444445555555555666
                 //12345678901234567890123456789012345678901234567890123456789012
  xterm.print(1,1,"╔════╦════════════════════════════════╦═══════╦═══════╦═══════════╗",NORMAL); 
  xterm.print(2,1,"║ ## ║ Network name                   ║ RSSI  ║ Avg   ║ Del./lost ║",NORMAL); 
  xterm.print(3,1,"╠════╬════════════════════════════════╬═══════╬═══════╬═══════════╣",NORMAL); 

  for (int i=0; i<data.size(); i++) writeMid(i+4);
  writeBot(data.size()+4);
  //xterm.print(4,1,"╚════╩════════════════════════════════╩═══════╩═══════╩═══════════╝",NORMAL); 
  return true;
}

bool writeScreen1(String ssid) {
  xterm.clear();

                 //00000000011111111112222222222333333333344444444445555555555666
                 //12345678901234567890123456789012345678901234567890123456789012
  xterm.print(1,1,"╔═════════════════════════════════════════════════════════════════╗",NORMAL); 
  xterm.print(2,1,"║                                                                 ║",NORMAL); 
  xterm.print(3,1,"╠═════════════════════════════════════════════════════════════════╣",NORMAL); 
  //for (int row=4; row<SCANS_COUNT/2+4+(SCANS_COUNT%2)+1; row++) 
  int rc=scans_max-scans_min+1;
  rc = rc / 2 + rc % 2;
  for (int row=4; row<rc+4; row++) 
    xterm.print(row,1,"║                                                                 ║",NORMAL); 
  //xterm.print(SCANS_COUNT/2+4+(SCANS_COUNT%2)+1,1,"╚═════════════════════════════════════════════════════════════════╝",NORMAL); 
  xterm.print(rc+4,1,"╚═════════════════════════════════════════════════════════════════╝",NORMAL); 
  xterm.print(2,3,ssid,NORMAL);
  drawMode1XtermFrameIfNeeded(true);
  return true;
}

String getEncryptionType(wifi_auth_mode_t encryptionType) {
    switch (encryptionType) {
        case WIFI_AUTH_OPEN: return "Open";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA*";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPAE";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA+";
        case WIFI_AUTH_WAPI_PSK: return "WPAI";
        case WIFI_AUTH_MAX: return "MAX";
        default: return "UNKN";
    };
}

void drawMode0Xterm(int n) {
    //std::sort(data.begin(),data.end(),[](datatype &a, datatype &b){ return a.sumRSSI/a.count>b.sumRSSI/b.count; });
    std::sort(data.begin(),data.end(),[](datatype &a, datatype &b){ 
        return a.sumRSSI*a.scan_count/a.count/a.unique_count  > b.sumRSSI*b.scan_count/b.count/b.unique_count; 
    });

    if (rc!=data.size()) writeBot(data.size()+4);
    while (rc<data.size())writeMid(rc++ + 4);
        
    int i=1;
    for (datatype &d : data) {
        //30 chars
        char name[31] = "                              ";
        memcpy(name,d.name.c_str(),d.name.length()>30?30:d.name.length());
        for (int j = 0; j<strlen(name); j++ ) if (name[j]>127) name[j] = '?'; //replace non-ascii chars
        
        xterm.printf(i+3,3,NORMAL,"%d ",i);
        //xterm.print(i+3,7,"                                ",NORMAL);
        xterm.printf(i+3,8,NORMAL,"%s",name);
        xterm.printf(i+3,41,NORMAL,"%d  ",d.lastRSSI);
        xterm.printf(i+3,49,NORMAL,"%d  ",d.sumRSSI/d.count);

        //delay:
        //xterm.printf(i+3,57,NORMAL,"%d  ",(millis()-d.last)/1000);
        if ((millis()-d.last)/1000 > 60) {
            xterm.printf(i+3,57,NORMAL,"%d s ",(millis()-d.last)/1000);
        } else {
            //char lost[16];
            //sprintf(lost,"%d%%    ",100*(d.scan_count-d.count)/d.scan_count);
            //lost[4] = 0;
            xterm.printf(i+3,57,NORMAL,"%d%%  ",100*(d.scan_count-d.unique_count)/d.scan_count);
        }

        xterm.printf(i+3,34,NORMAL,getEncryptionType(d.encryptionType).c_str());

        //xterm.printf(i+3,70,NORMAL,"%d",d.scan_count);
        //xterm.printf(i+3,75,NORMAL,"%d",d.count);
        //xterm.printf(i+3,80,NORMAL,"%d",d.unique_count);
        if (vmode==1) {
            if (d.mesh_size>1) xterm.printf(i+3,70,NORMAL,"mesh %d",d.mesh_size);
            else xterm.printf(i+3,70,NORMAL,"       ");
        }
        
   
        i++;
    }
    xterm.printf(rc+5,1,NORMAL,"found %d networks; uptime %d seconds      ",n,millis()/1000);
}

void drawMode0(int n) {
    //Non-xterm mode. order oposite, show last records only
    //std::sort(data.begin(),data.end(),[](datatype &a, datatype &b){ return a.sumRSSI/a.count<b.sumRSSI/b.count; });
    std::sort(data.begin(),data.end(),[](datatype &a, datatype &b){ 
        return a.sumRSSI*a.scan_count/a.count/a.unique_count  < b.sumRSSI*b.scan_count/b.count/b.unique_count; 
    });

    Serial.printf("========%d sec; %d networks=====\n",millis()/1000,n);
    if (vmode==1) 
        Serial.printf("# | RSSI | Avg | lost | delay | mesh | cnt | encr | Name\n",millis()/1000,n);
    else
        Serial.printf("# | RSSI | Avg | lost | delay | Name\n",millis()/1000,n);
    int i=1;
    
    for (datatype &d : data) if ((i+32)>(data.size())) {
        
        char name[31] = {0};
        memcpy(name,d.name.c_str(),d.name.length()>30?30:d.name.length());
        for (int j = 0; j<strlen(name); j++ ) if (name[j]>127) name[j] = '?'; //replace non-ascii chars
        name[d.name.length()>30?30:d.name.length()] = 0;

        char del[16];
        sprintf(del,"%d     ",(millis()-d.last)/1000);
        del[5] = 0;

        char lost[16];
        sprintf(lost,"%d%%    ",100*(d.scan_count-d.unique_count)/d.scan_count);
        lost[4] = 0;

        

        if (vmode==1) {
            Serial.printf("%02d | %03d | %03d | %s | %s |  %02d  | %03d | %s | %s\n",i,d.lastRSSI,(int)(d.sumRSSI/d.count),lost,del,d.mesh_size,d.unique_count,getEncryptionType(d.encryptionType).c_str(),name);
        } else {
            Serial.printf("%02d | %03d | %03d | %s | %s | %s\n",i,d.lastRSSI,(int)(d.sumRSSI/d.count),lost,del,name);
        }

        i++;
    } else i++;
}

int get_scans_c(int32_t rssi, uint8_t range=50) {
    if (rssi==0) return 0;
    if (scans_max==scans_min) return range/2;
    return (range-1)*(rssi-scans_min)/(scans_max-scans_min)+1;
}

void drawMode1Xterm_old(String ssid) {
    
    /*
    int32_t min = 0;
    int32_t max = -127;
    for (auto &rssi : scans) {
        if (rssi && rssi>max) max = rssi;
        if (rssi && rssi<min) min = rssi;
    }
    */
    if (scans_max<scans_min || data.empty()) return; //no data
    int row = 4;
    for (auto &rssi : scans) {
        //int row = 1 + (rssi-min)*20/(max-min);
        xterm.print(row,2,"   ",NORMAL);
        xterm.print(row,1,rssi,NORMAL);

        int c = get_scans_c(rssi);

        String s1 =""; for (int i=5; i<c+5; i++) s1+="█";
        String s2 =""; for (int i=c+5; i<55; i++) s2+=" ";
        xterm.print(row,5,s1,NORMAL);
        xterm.print(row,c+5,s2,NORMAL);
        row++;
    }

};

int _dm1max = 0;
int _dm1min = 0;
void drawMode1XtermFrameIfNeeded(bool rebuild=false) {
    if (_dm1min==scans_min && _dm1max==scans_max && !rebuild) return;
    
    int rc=_dm1max-_dm1min+1;
    int oldrc=rc/2+rc%2;
    _dm1min = scans_min;
    _dm1max = scans_max;

    rc=_dm1max-_dm1min+1;
    rc=rc/2+rc%2;

    //redraw bottom line if needed
    if (oldrc!=rc) {
        xterm.print(oldrc+4,1,"║                                                                 ║",NORMAL);
        xterm.print(rc+4,1,   "╚═════════════════════════════════════════════════════════════════╝",NORMAL);
    }

    for (int i=0; i<rc; i+=2) {
        //i=0:scans_max; i=rc:scans_min
        int rssi = scans_max - (scans_max-scans_min)*i/(rc-1);
        xterm.print(i+4,3,rssi,NORMAL);  
        xterm.print(i+4,63,rssi,NORMAL);  
    }

}

uint8_t saved_y[SCANS_COUNT]={0};
void drawMode1Xterm(String ssid) {
    drawMode1XtermFrameIfNeeded();
    // ▀▄█▌▐▄▀
    if (scans_max<scans_min || data.empty()) return; //no data
    if (scans.empty()) return;

    //calc stat
    int sum = 0;
    int loss = 0;
    for (auto &rssi : scans) {
        if (rssi==0) loss++;
        else sum+=rssi;
    }
    int avg = scans.size()>loss?sum/((int)(scans.size()-loss)):0;
    //int xxx = sum/10;

    //display stat
    xterm.print(2,45,"                     ",NORMAL);
    //char s[22];
    //sprintf(s,"avg %d; %d\% lost",avg,loss*100/scans.size());
    //xterm.print(2,45,s,NORMAL);
    xterm.print(2,45,"avg:",NORMAL);
    xterm.print(2,50,avg,NORMAL);
    xterm.print(2,55,"lost",NORMAL);
    int lost = loss*100/scans.size();
    xterm.print(2,60,lost,NORMAL);
    xterm.print(2,64,"%",NORMAL);
    //xterm.print(6,50,xxx,NORMAL);
    


    //going by scans end erase old data, draw new data
    int rc=scans_max-scans_min+1;
    rc = rc / 2 + rc % 2;
    int i=0;
    for (auto &rssi : scans) {
        //int c = get_scans_c(rssi);
        

        xterm.print(saved_y[i]+4,i+10," ",NORMAL);

        if (rssi==0) {
          xterm.print(rc-1+4,i+10,"X",NORMAL);    
          saved_y[i] = rc-1;
        } else {
            int c = rssi-scans_min;
            int y = rc - 1 - c/2 - c%2;
            
            xterm.print(y+4,i+10,c%2?"▄":"▀",NORMAL);
            //xterm.print(y+4,i+11,rssi,NORMAL);
            //xterm.print(y+4,63,rssi,NORMAL);

            saved_y[i] = y;
        
        };
        i++;
    }
    

};

void drawMode1(String ssid) {
    /*
    Serial.printf("====== %s =====\n",ssid.c_str());
    for (auto &rssi : scans) {
        Serial.printf("%d",rssi);
        Serial.printf("\n");
    }
    */
    if (scans_max<scans_min || data.empty()) return; //no data
    if (scans.empty()) return;

    int c = get_scans_c(scans.back());
    String res = "";
    for (int i=0; i<c; i++) res+="█";

    Serial.printf("%d ",scans.back());
    Serial.printf("%s\n",res.c_str());
};