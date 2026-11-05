#pragma once
struct PDef{const char* id;const char* name;float mn,mx,def;int kind;const char* choices;}; // kind: 0 lin, 1 log, 2 scelta, 3 on/off
inline const PDef PARAMS[]={
    {"main_input","Main input",0.0f,10.0f,0.0f,0,""},
    {"main_high","Main high",-15.0f,15.0f,0.0f,0,""},
    {"main_pan","Main pan",-1.0f,1.0f,0.0f,0,""},
    {"main_mid_gain","Main mid gain",-15.0f,15.0f,0.0f,0,""},
    {"main_low_gain","Main low gain",-15.0f,15.0f,0.0f,0,""},
    {"main_mid_freq","Main mid freq",300.0f,10000.0f,1732.0508075688772f,1,""},
    {"main_low_freq","Main low freq",40.0f,800.0f,178.88543819998318f,1,""},
    {"main_comp","Main comp",0.0f,1.0f,0.0f,0,""},
    {"main_fader","Fader dB",-80.0f,10.0f,0.0f,0,""},
    {"channel","Channel",0.0f,7.0f,0.0f,2,"1|2|3|4|5|6|7|8"},
    {"main_bypass","Bypass",0.0f,1.0f,0.0f,3,""},
    {"main_micline","Mic / Line",0.0f,1.0f,0.0f,3,""},
    {"main_hiss","Hiss",0.0f,1.0f,0.5f,0,""},
    {"tape_cal","Tape level",0.0f,3.0f,0.0f,2,"185 nWb/m|250 nWb/m|320 nWb/m|500 nWb/m"},
};
