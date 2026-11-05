#pragma once
// Generato da geo.json (immagine 1380x752): coordinate di manopole, VU, fader, interruttori.
struct KnobDef{const char* pid;float cx,cy,cap;};
struct FaceDef{float x0,y0,x1,y1,cx,py,R,th,H;};
struct FaderDef{float top,bot,slot,capCx;};
inline constexpr float IMG_W=1380.0f, IMG_H=752.0f;
inline const KnobDef KNOBS[]={
    {"main_input",300.0f,317.0f,23.0f},
    {"main_high",519.0f,317.0f,21.0f},
    {"main_mid_gain",446.0f,462.0f,23.0f},
    {"main_mid_freq",597.0f,462.0f,24.0f},
    {"main_low_gain",445.0f,610.0f,23.0f},
    {"main_low_freq",596.0f,610.0f,24.0f},
    {"main_pan",822.0f,324.0f,31.0f},
    {"main_comp",820.0f,516.0f,25.0f},
    {"main_hiss",304.0f,578.0f,16.0f},
};
inline const FaceDef FACES[2]={
    {714.0f,77.0f,917.0f,180.0f,815.50f,223.26f,118.45f,47.0f,103.0f},
    {951.0f,77.0f,1154.0f,180.0f,1052.50f,223.26f,118.45f,47.0f,103.0f},
};
inline const FaderDef FADER={291.5f,612.5f,1066.0f,1070.0f};
inline constexpr int FADER_N=14;
inline const float FADER_DB[FADER_N]={10.0f,6.0f,3.0f,0.0f,-3.0f,-6.0f,-10.0f,-15.0f,-20.0f,-30.0f,-40.0f,-50.0f,-60.0f,-80.0f};
// rettangoli x0,y0,x1,y1
inline const float MIC_BOX[4]={286.0f,419.0f,340.0f,442.0f};
inline const float BYP_BOX[4]={794.0f,636.0f,846.0f,663.0f};
inline const float TAPE_BOX[4]={975.0f,648.0f,1160.0f,714.0f};
inline const float LED_POS[2]={820.0f,596.0f};
inline const float CAL_BOX[4]={265.0f,172.0f,425.0f,206.0f};   // finestrella TAPE LEVEL sotto "8-TRACK RECORDER/MIXER"
