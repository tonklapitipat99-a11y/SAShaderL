#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>

#include <stdio.h>
#include <string.h>
#include "ES3Shader.h"

//#define DUMP_SHADERS

// ==========================================
// ประกาศตัวแปร FLAG ที่ขาดหายไป เพื่อแก้ Error แดง
// ==========================================
#define FLAG_ALPHA_TEST             0x1
#define FLAG_LIGHTING               0x2
#define FLAG_ALPHA_MODULATE         0x4
#define FLAG_COLOR_EMISSIVE         0x8
#define FLAG_COLOR                  0x10
#define FLAG_TEX0                   0x20
#define FLAG_ENVMAP                 0x40
#define FLAG_BONE3                  0x80
#define FLAG_BONE4                  0x100
#define FLAG_CAMERA_BASED_NORMALS   0x200
#define FLAG_FOG                    0x400
#define FLAG_TEXBIAS                0x800
#define FLAG_BACKLIGHT              0x1000
#define FLAG_LIGHT1                 0x2000
#define FLAG_LIGHT2                 0x4000
#define FLAG_LIGHT3                 0x8000
#define FLAG_DETAILMAP              0x10000
#define FLAG_COMPRESSED_TEXCOORD    0x20000
#define FLAG_PROJECT_TEXCOORD       0x40000
#define FLAG_WATER                  0x80000
#define FLAG_COLOR2                 0x100000
#define FLAG_SPHERE_XFORM           0x200000
#define FLAG_SPHERE_ENVMAP          0x400000
#define FLAG_TEXMATRIX              0x800000
#define FLAG_GAMMA                  0x4000000
#define FLAG_CUSTOM_SKY             0x08000000
#define FLAG_CUSTOM_BUILDING        0x10000000

class SASL : public ISASL
{
public:
    inline void ResetApplyFlagForShaders(int customUniformId)
    {
        for(ES3Shader* shader : g_AllShaders)
        {
            shader->uniforms[customUniformId].needToApply = true;
        }
    }
    int GetFeaturesVersion() { return 1; }
    int RegisterUniform(const char* name, eUniformValueType type, uint8_t valuesArraySize, bool alwaysUpdate, void* pointer)
    {
        if(CustomStaticUniform::registeredUniforms >= CUSTOM_UNIFORMS) return -1;

        int id = CustomStaticUniform::registeredUniforms;
        staticUniforms[id].name = name;
        staticUniforms[id].count = valuesArraySize;
        staticUniforms[id].type = type;
        staticUniforms[id].alwaysUpdate = alwaysUpdate;
        staticUniforms[id].data.iptr = (int*)pointer;
        ++CustomStaticUniform::registeredUniforms;
        return id;
    }
    void SetUniformInt(int id, int dataNum, int value)
    {
        staticUniforms[id].SetInt(dataNum, value);
        if(staticUniforms[id].IsChanged()) ResetApplyFlagForShaders(id);
    }
    void SetUniformUInt(int id, int dataNum, uint32_t value)
    {
        staticUniforms[id].SetUInt(dataNum, value);
        if(staticUniforms[id].IsChanged()) ResetApplyFlagForShaders(id);
    }
    void SetUniformFloat(int id, int dataNum, float value)
    {
        staticUniforms[id].SetFloat(dataNum, value);
        if(staticUniforms[id].IsChanged()) ResetApplyFlagForShaders(id);
    }
    void SetUniformPtr(int id, int dataNum, void* ptr)
    {
        staticUniforms[id].SetPtr(dataNum, ptr);
        if(staticUniforms[id].IsChanged()) ResetApplyFlagForShaders(id);
    }
    void ForceUpdateData(int id)
    {
        ResetApplyFlagForShaders(id);
    }
} sasl;

MYMOD(net.rusjj.sashader, SAShaderLoader, 1.1, RusJJ)
NEEDGAME(com.rockstargames.gtasa)

// Savings
#define SHADER_LEN                                  (32 * 1024) 
#define FRAGMENT_SHADER_STORAGE(__var1, __var2)     sprintf(__var1, "%s/shaders/fragment/" #__var2 ".glsl", aml->GetAndroidDataPath());
#define VERTEX_SHADER_STORAGE(__var1, __var2)       sprintf(__var1, "%s/shaders/vertex/" #__var2 ".glsl", aml->GetAndroidDataPath());
#define FRAGMENT_SHADER_GEN_STORAGE(__var1, __var2) sprintf(__var1, "%s/shaders/fragment/gen/%s.glsl", aml->GetAndroidDataPath(), __var2);
#define VERTEX_SHADER_GEN_STORAGE(__var1, __var2)   sprintf(__var1, "%s/shaders/vertex/gen/%s.glsl", aml->GetAndroidDataPath(), __var2);

uintptr_t pGTASA;
void *hGTASA, *hGLES;
char blurShaderOwn[SHADER_LEN + 1], gradingShaderOwn[SHADER_LEN + 1], shadowResolveOwn[SHADER_LEN + 1], contrastVertexOwn[SHADER_LEN + 1], contrastFragmentOwn[SHADER_LEN + 1];
char customPixelShader[SHADER_LEN + 1], customVertexShader[SHADER_LEN + 1];
int lastModelId = -1;

// Game Vars
const char **blurShader, **gradingShader, **shadowResolve, **contrastVertex, **contrastFragment;
CVector *m_VectorToSun;
int *m_CurrentStoredValue;
uint32_t *m_snTimeInMilliseconds, *curShaderStateFlags;
uint8_t *ms_nGameClockSeconds, *ms_nGameClockMinutes;
float *UnderWaterness, *WetRoads;
CCamera* TheCamera;
ES3Shader** fragShaders;
ES3Shader** activeShader;

// Own Funcs
inline void freadfull(char* buf, size_t maxlen, FILE *f)
{
    size_t i = 0;
    --maxlen;
    while(!feof(f) && i<maxlen)
    {
        buf[i] = fgetc(f);
        ++i;
    }
    buf[i-1] = 0;
}

inline const char* FlagsToShaderName(int flags, bool isVertex)
{
    if(flags == 0x421) return "reqqqqq";
    if(isVertex)
    {
        switch(flags)
        {
            case 0x10:
            case 0x200010: return "untextured2D";
            case 0x4000010: return "gammaColor2D";
            case 0x80430: return "water";
            case 0x90430: return "waterDetailed";
            case 0x8000010: return "sky";
            case 0x1010040A: return "building/untextured";
            case 0x10020430: return "building/textured_compressedTex";
            case 0x12020430: return "building/textured_compressedTex_normal";
            case 0x10220432: return "building/textured_compressedTex_light";
            case 0x10222432: return "building/textured_compressedTex_light2";
            case 0x1010042A: return "building/textured2Colors_light";
            case 0x1013042A:
            case 0x1012042A: return "building/textured2Colors_comp_light";
            case 0x10110430:
            case 0x10100430: return "building/textured2Colors";
            case 0x1092042A: return "building/textured2Colors_xenv";
            case 0x10120434:
            case 0x10120630:
            case 0x10130430:
            case 0x10120430:
            case 0x1011042A: return "building/textured2Colors_light";
        }
    }
    else 
    {
        switch(flags)
        {
            case 0x10:
            case 0x200010: return "untextured2D";
            case 0x4000010: return "gammaColor2D";
            case 0x80430: return "water";
            case 0x90430: return "waterDetailed";
            case 0x1010040A: return "building/untextured";
            case 0x10020430:
            case 0x12020430:
            case 0x10220432:
            case 0x10222432: return "building/textured";
            case 0x10100430:
            case 0x10120430:
            case 0x10120630:
            case 0x1010042A:
            case 0x1012042A:
            case 0x1092042A:
            case 0x10120434:
            case 0x10110430:
            case 0x10130430:
            case 0x1013042A:
            case 0x1011042A: return "building/textured2Colors";
            case 0x8000010: return "sky";
        }
    }
    return NULL;
}

template <size_t size>
inline void FlagToName(int flags, char (&out)[size])
{
    out[0] = 0;
    if(flags & FLAG_ALPHA_TEST) strlcat(out, "Atest", size);
    if(flags & FLAG_LIGHTING) strlcat(out, "Light", size);
    if(flags & FLAG_ALPHA_MODULATE) strlcat(out, "Mod", size);
    if(flags & FLAG_COLOR_EMISSIVE) strlcat(out, "Emiss", size);
    if(flags & FLAG_COLOR) strlcat(out, "Color", size);
    if(flags & FLAG_TEX0) strlcat(out, "T0", size);
    if(flags & FLAG_ENVMAP) strlcat(out, "Env", size);
    if(flags & FLAG_BONE3) strlcat(out, "B3", size);
    if(flags & FLAG_BONE4) strlcat(out, "B4", size);
    if(flags & FLAG_CAMERA_BASED_NORMALS) strlcat(out, "Norm", size);
    if(flags & FLAG_FOG) strlcat(out, "Fog", size);
    if(flags & FLAG_TEXBIAS) strlcat(out, "Bias", size);
    if(flags & FLAG_BACKLIGHT) strlcat(out, "Backl", size);
    if(flags & FLAG_LIGHT1) strlcat(out, "Light1", size);
    if(flags & FLAG_LIGHT2) strlcat(out, "Light2", size);
    if(flags & FLAG_LIGHT3) strlcat(out, "Light3", size);
    if(flags & FLAG_DETAILMAP) strlcat(out, "Detail", size);
    if(flags & FLAG_COMPRESSED_TEXCOORD) strlcat(out, "Comp", size);
    if(flags & FLAG_PROJECT_TEXCOORD) strlcat(out, "Proj", size);
    if(flags & FLAG_WATER) strlcat(out, "Water", size);
    if(flags & FLAG_COLOR2) strlcat(out, "Color2", size);
    if(flags & FLAG_SPHERE_XFORM) strlcat(out, "Xform", size);
    if(flags & FLAG_SPHERE_ENVMAP) strlcat(out, "Envmap", size);
    if(flags & FLAG_TEXMATRIX) strlcat(out, "Matrix", size);
    if(flags & FLAG_GAMMA) strlcat(out, "Gamma", size);
    if(flags & FLAG_CUSTOM_SKY) strlcat(out, "Sky", size);
    if(flags & FLAG_CUSTOM_BUILDING) strlcat(out, "Building", size);
}

// Game Funcs
EmuShader* (*emu_CustomShaderCreate)(const char* fragShad, const char* vertShad);
int (*_glGetUniformLocation)(int, const char*);
void (*_glUniform1i)(int, int);
void (*_glUniform1fv)(int, int, const float*);
void (*_glUniform1iv)(int, int, const int*);
void (*_glUniform1uiv)(int, int, const unsigned int*);

DECL_HOOK(int, RQShaderBuildSource, int flags, char **pxlsrc, char **vtxsrc)
{
    int ret = RQShaderBuildSource(flags, pxlsrc, vtxsrc);
    FILE *pFile;
    char szTmp[256], szNameCat[128];
    FlagToName(flags, szNameCat);
    
    #ifdef DUMP_SHADERS
        const char* fragName = FlagsToShaderName(flags, false);
        if(fragName)
        {
            FRAGMENT_SHADER_GEN_STORAGE(szTmp, fragName);
            pFile = fopen(szTmp, "w");
            if(pFile != NULL)
            {
                fwrite(*pxlsrc, 1, strlen(*pxlsrc), pFile);
                fclose(pFile);
            }
        }
        else
        {
            sprintf(szTmp, "%s/shaders/f/F_%s_0x%X.glsl", aml->GetAndroidDataPath(), szNameCat, flags);
            logger->Info(szTmp);
            pFile = fopen(szTmp, "w");
            if(pFile != NULL)
            {
                fwrite(*pxlsrc, 1, strlen(*pxlsrc), pFile);
                fclose(pFile);
            }
        }
        
        const char* vertName = FlagsToShaderName(flags, true);
        if(vertName)
        {
            VERTEX_SHADER_GEN_STORAGE(szTmp, vertName);
            pFile = fopen(szTmp, "w");
            if(pFile != NULL)
            {
                fwrite(*vtxsrc, 1, strlen(*vtxsrc), pFile);
                fclose(pFile);
            }
        }
        else
        {
            sprintf(szTmp, "%s/shaders/v/F_%s_0x%X.glsl", aml->GetAndroidDataPath(), szNameCat, flags);
            pFile = fopen(szTmp, "w");
            if(pFile != NULL)
            {
                fwrite(*vtxsrc, 1, strlen(*vtxsrc), pFile);
                fclose(pFile);
            }
        }
    #else
        const char* fragName = FlagsToShaderName(flags, false);
        if(fragName)
        {
            FRAGMENT_SHADER_GEN_STORAGE(szTmp, fragName);
            pFile = fopen(szTmp, "r");
            if(pFile != NULL)
            {
                logger->Info("Loading custom fragment shader \"%s\"", fragName);
                freadfull(*pxlsrc, SHADER_LEN, pFile);
                fclose(pFile);
            }
        }
        
        const char* vertName = FlagsToShaderName(flags, true);
        if(vertName)
        {
            VERTEX_SHADER_GEN_STORAGE(szTmp, vertName);
            pFile = fopen(szTmp, "r");
            if(pFile != NULL)
            {
                logger->Info("Loading custom vertex shader \"%s\"", vertName);
                freadfull(*vtxsrc, SHADER_LEN, pFile);
                fclose(pFile);
            }
        }
    #endif
    
    return ret;
}

DECL_HOOKv(InitES2Shader, ES3Shader* self)
{
    InitES2Shader(self);

    memset(self->uniforms, 0, sizeof(self->uniforms));
    g_AllShaders.push_back(self);
    
    for(int i = 0; i < CustomStaticUniform::registeredUniforms; ++i)
    {
        self->uniforms[i].uniformId = _glGetUniformLocation(self->nShaderId, staticUniforms[i].name);
    }
}

DECL_HOOKv(RQ_Command_rqSelectShader, ES3Shader*** ptr)
{
    ES3Shader* shader = **ptr;
    RQ_Command_rqSelectShader(ptr);

    for(int i = 0; i < CustomStaticUniform::registeredUniforms; ++i)
    {
        CustomUniform& uniform = shader->uniforms[i];
        if(uniform.uniformId == -1) continue;

        CustomStaticUniform& staticUniform = staticUniforms[i];
        if(staticUniform.alwaysUpdate || uniform.needToApply)
        {
            uniform.needToApply = false;
            switch(staticUniform.type)
            {
                case UNIFORM_INT:
                    _glUniform1iv (uniform.uniformId, staticUniform.count, staticUniform.data.iptr ? staticUniform.data.iptr : &staticUniform.data.i[0]);
                    break;
                case UNIFORM_UINT:
                    _glUniform1uiv(uniform.uniformId, staticUniform.count, staticUniform.data.uptr ? staticUniform.data.uptr : &staticUniform.data.u[0]);
                    break;
                case UNIFORM_FLOAT:
                    _glUniform1fv (uniform.uniformId, staticUniform.count, staticUniform.data.fptr ? staticUniform.data.fptr : &staticUniform.data.f[0]);
                    break;
                default:
                    break;
            }
        }
    }
}

DECL_HOOKv(RenderSkyPolys)
{
    *curShaderStateFlags |= FLAG_CUSTOM_SKY;
    RenderSkyPolys();
    *curShaderStateFlags &= ~FLAG_CUSTOM_SKY;
}

DECL_HOOKv(OnEntityRender, CEntity* self)
{
    lastModelId = self->m_nModelIndex;
    if(self->m_nType == ENTITY_TYPE_BUILDING)
    {
        *curShaderStateFlags |= FLAG_CUSTOM_BUILDING;
        OnEntityRender(self);
        *curShaderStateFlags &= ~FLAG_CUSTOM_BUILDING;
        lastModelId = -1;
        return;
    }
    OnEntityRender(self);
    lastModelId = -1;
}

// Patch funcs
#ifdef AML32
uintptr_t BuildShader_BackTo;
__attribute__((optnone)) __attribute__((naked)) void BuildShader_inject(void)
{
    asm volatile(
        "MOV R5, R0\n"
        "MOV R8, R2\n");
    asm volatile(
        "MOV R0, %0\n"
    :: "r" (sizeof(ES3Shader)));
    asm volatile(
        "PUSH {R0}\n");
    asm volatile(
        "MOV R12, %0\n"
        "POP {R0}\n"
        "BX R12\n"
    :: "r" (BuildShader_BackTo));
}
#else
inline void ReplaceADRL(uintptr_t addr, uint32_t firstVal, uint32_t secVal)
{
    aml->Write32(addr, firstVal);
    aml->Write32(addr + 0x4, secVal);
}
#endif

// int main!
extern "C" void OnModPreLoad()
{
    sasl.RegisterUniform("Time", UNIFORM_UINT, 1, true, m_snTimeInMilliseconds);
    sasl.RegisterUniform("UnderWaterness", UNIFORM_FLOAT, 1, true, UnderWaterness);
    RegisterInterface("SASL", &sasl);
}

extern "C" void OnModLoad()
{
    logger->SetTag("SA ShaderLoader");

    pGTASA = aml->GetLib("libGTASA.so");
    hGTASA = aml->GetLibHandle("libGTASA.so");
    if(!pGTASA || !hGTASA)
    {
        logger->Error("GTA:SA is not loaded :o");
        return;
    }
    
    hGLES = aml->GetLibHandle("libGLESv2.so");
    if(!hGLES)
    {
        logger->Error("Open GLES is not loaded :o");
        return;
    }
    
    SET_TO(blurShader, aml->GetSym(hGTASA, "blurPShader"));
    SET_TO(gradingShader, aml->GetSym(hGTASA, "gradingPShader"));
    SET_TO(shadowResolve, aml->GetSym(hGTASA, "shadowResolvePShader"));
    SET_TO(contrastVertex, aml->GetSym(hGTASA, "contrastVShader"));
    SET_TO(contrastFragment, aml->GetSym(hGTASA, "contrastPShader"));
    
    HOOKPLT(RQShaderBuildSource, pGTASA + BYBIT(0x6720F8, 0x8439A0));
    
    FILE *pFile;
    char szTmp[256];
    
    FRAGMENT_SHADER_STORAGE(szTmp, blur);
    if((pFile = fopen(szTmp, "r"))!=NULL)
    {
        freadfull(blurShaderOwn, SHADER_LEN, pFile);
        *blurShader = blurShaderOwn;
        fclose(pFile);
    }
    
    FRAGMENT_SHADER_STORAGE(szTmp, grading);
    if((pFile = fopen(szTmp, "r"))!=NULL)
    {
        freadfull(gradingShaderOwn, SHADER_LEN, pFile);
        *gradingShader = gradingShaderOwn;
        fclose(pFile);
    }
    
    FRAGMENT_SHADER_STORAGE(szTmp, shadowResolve);
    if((pFile = fopen(szTmp, "r"))!=NULL)
    {
        freadfull(shadowResolveOwn, SHADER_LEN, pFile);
        *shadowResolve = shadowResolveOwn;
        fclose(pFile);
    }
    
    VERTEX_SHADER_STORAGE(szTmp, contrast);
    if((pFile = fopen(szTmp, "r"))!=NULL)
    {
        freadfull(contrastVertexOwn, SHADER_LEN, pFile);
        *contrastVertex = contrastVertexOwn;
        fclose(pFile);
    }
    
    FRAGMENT_SHADER_STORAGE(szTmp, contrast);
    if((pFile = fopen(szTmp, "r"))!=NULL)
    {
        freadfull(contrastFragmentOwn, SHADER_LEN, pFile);
        *contrastFragment = contrastFragmentOwn;
        fclose(pFile);
    }
    
  #ifdef AML32
    BuildShader_BackTo = pGTASA + 0x1CD838 + 0x1;
    aml->Redirect(pGTASA + 0x1CD830 + 0x1, (uintptr_t)BuildShader_inject);
  #else
    // แก้บัค Error MOVBits ตรงนี้!
    aml->Write32(pGTASA + 0x262AB0, 0x52800000 | ((sizeof(ES3Shader) & 0xFFFF) << 5));
  #endif

  #ifdef AML32
    HOOKPLT(InitES2Shader, pGTASA + 0x671BDC);
    HOOKPLT(RQ_Command_rqSelectShader, pGTASA + 0x67632C);
    HOOKPLT(RenderSkyPolys, pGTASA + 0x670A7C);
    HOOK(OnEntityRender, aml->GetSym(hGTASA, "_ZN7CEntity6RenderEv"));
  #else
    HOOKBL(InitES2Shader, pGTASA + 0x26213C);
    HOOKPLT(RQ_Command_rqSelectShader, pGTASA + 0x84A6B0);
    HOOKPLT(RenderSkyPolys, pGTASA + 0x8414C8);
    HOOK(OnEntityRender, aml->GetSym(hGTASA, "_ZN7CEntity6RenderEv"));
  #endif

    SET_TO(_glGetUniformLocation, *(void**)(pGTASA + BYBIT(0x6755EC, 0x8403B0)));
    SET_TO(_glUniform1i, *(void**)(pGTASA + BYBIT(0x674484, 0x846858)));
    SET_TO(_glUniform1fv, *(void**)(pGTASA + BYBIT(0x672388, 0x845048)));
    SET_TO(emu_CustomShaderCreate, aml->GetSym(hGTASA, "_Z22emu_CustomShaderCreatePKcS0_"));
    SET_TO(_glUniform1iv, aml->GetSym(hGLES, "glUniform1iv"));
    SET_TO(_glUniform1uiv, aml->GetSym(hGLES, "glUniform1uiv"));

    SET_TO(fragShaders, pGTASA + BYBIT(0x6B408C, 0x891058));
    SET_TO(activeShader, aml->GetSym(hGTASA, "_ZN9ES2Shader12activeShaderE"));
    SET_TO(m_VectorToSun, aml->GetSym(hGTASA, "_ZN10CTimeCycle13m_VectorToSunE"));
    SET_TO(m_CurrentStoredValue, aml->GetSym(hGTASA, "_ZN10CTimeCycle20m_CurrentStoredValueE"));
    SET_TO(m_snTimeInMilliseconds, aml->GetSym(hGTASA, "_ZN6CTimer22m_snTimeInMillisecondsE"));
    SET_TO(curShaderStateFlags, aml->GetSym(hGTASA, "curShaderStateFlags"));
    SET_TO(ms_nGameClockMinutes, aml->GetSym(hGTASA, "_ZN6CClock20ms_nGameClockMinutesE"));
    SET_TO(ms_nGameClockSeconds, aml->GetSym(hGTASA, "_ZN6CClock20ms_nGameClockSecondsE"));
    SET_TO(UnderWaterness, aml->GetSym(hGTASA, "_ZN8CWeather14UnderWaternessE"));
    SET_TO(WetRoads, aml->GetSym(hGTASA, "_ZN8CWeather8WetRoadsE"));
    SET_TO(TheCamera, aml->GetSym(hGTASA, "TheCamera"));
    
  #ifdef AML32
    aml->WriteAddr(pGTASA + 0x1CF73C, (uintptr_t)&customVertexShader - pGTASA - 0x1CEA48);
    aml->WriteAddr(pGTASA + 0x1CF7AC, (uintptr_t)&customVertexShader - pGTASA - 0x1CEAD0);
    aml->WriteAddr(pGTASA + 0x1CF7C0, (uintptr_t)&customVertexShader - pGTASA - 0x1CEB44);
    aml->WriteAddr(pGTASA + 0x1CF7CC, (uintptr_t)&customVertexShader - pGTASA - 0x1CEB50);
  #endif
}
