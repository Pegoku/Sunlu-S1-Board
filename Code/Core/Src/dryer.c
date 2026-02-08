/**
 * @file    dryer.c
 * @brief   Filament Dryer – flash-optimised (no float, minimal font)
 *
 * Fits in 16 KB flash on STM32C011xx.
 * Integer-only NTC lookup, 29-glyph font (only used characters).
 */

#include "dryer.h"

/* ── HAL handles (defined in main.c) ───────────────────────────── */
extern SPI_HandleTypeDef hspi1;
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim17;

/* ── Display ───────────────────────────────────────────────────── */
#define XOFF  0
#define YOFF  0
#define SW  160
#define SH  128

/* RGB-565 colours */
#define C_BK  0x0000U
#define C_WH  0xFFFFU
#define C_CY  0x07FFU
#define C_YL  0xFFE0U
#define C_GN  0x07E0U
#define C_GR  0x8410U
#define C_DK  0x4208U
#define C_OR  0xFD20U

/* ── Dryer limits ──────────────────────────────────────────────── */
#define TMIN   30
#define TMAX   70
#define TDEF   50
#define MMIN   30
#define MMAX   720
#define MDEF   240
#define MSTP   30
#define TSAFE  85

/* ── Buttons ───────────────────────────────────────────────────── */
#define DEB   30
#define LNG   1000

/* ── Special char code for degree ────────────────────────────── */
#define CDEG  '\x01'

/* ══════════════════════════════════════════════════════════════════
 *  TYPES
 * ══════════════════════════════════════════════════════════════════ */
typedef enum { BN, BS, BL }           BE;
typedef enum { SM, SC, SD }           Scr;
typedef enum { SI, SR, SP }           DS;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t  pr, lr;
    uint32_t dt, pt;
    uint8_t  lf;
} Btn;

/* ══════════════════════════════════════════════════════════════════
 *  MINIMAL 5x7 FONT  (only characters actually used on screen)
 *
 *  Order: SPC : 0-9 A C D E G H I L M N O P R S T U DEG
 *  Index: 0   1 2..11  12..27                       28
 * ══════════════════════════════════════════════════════════════════ */
#define GN 29

static const uint8_t GF[GN][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /*  0 space */
    {0x00,0x36,0x36,0x00,0x00}, /*  1 :     */
    {0x3E,0x51,0x49,0x45,0x3E}, /*  2 0     */
    {0x00,0x42,0x7F,0x40,0x00}, /*  3 1     */
    {0x42,0x61,0x51,0x49,0x46}, /*  4 2     */
    {0x21,0x41,0x45,0x4B,0x31}, /*  5 3     */
    {0x18,0x14,0x12,0x7F,0x10}, /*  6 4     */
    {0x27,0x45,0x45,0x45,0x39}, /*  7 5     */
    {0x3C,0x4A,0x49,0x49,0x30}, /*  8 6     */
    {0x01,0x71,0x09,0x05,0x03}, /*  9 7     */
    {0x36,0x49,0x49,0x49,0x36}, /* 10 8     */
    {0x06,0x49,0x49,0x29,0x1E}, /* 11 9     */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 12 A     */
    {0x3E,0x41,0x41,0x41,0x22}, /* 13 C     */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 14 D     */
    {0x7F,0x49,0x49,0x49,0x41}, /* 15 E     */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 16 G     */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 17 H     */
    {0x00,0x41,0x7F,0x41,0x00}, /* 18 I     */
    {0x7F,0x40,0x40,0x40,0x40}, /* 19 L     */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 20 M     */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 21 N     */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 22 O     */
    {0x7F,0x09,0x09,0x09,0x06}, /* 23 P     */
    {0x7F,0x09,0x19,0x29,0x46}, /* 24 R     */
    {0x46,0x49,0x49,0x49,0x31}, /* 25 S     */
    {0x01,0x01,0x7F,0x01,0x01}, /* 26 T     */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 27 U     */
    {0x06,0x09,0x09,0x06,0x00}, /* 28 DEG   */
};

static const char lmap[] = "ACDEGHILMNOPRSTU";

static int8_t gidx(char c)
{
    if (c == ' ')  return 0;
    if (c == ':')  return 1;
    if (c >= '0' && c <= '9') return (int8_t)(2 + c - '0');
    if (c == CDEG) return 28;
    for (int8_t i = 0; i < 16; i++)
        if (lmap[(uint8_t)i] == c) return (int8_t)(12 + i);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
 *  APPLICATION STATE
 * ══════════════════════════════════════════════════════════════════ */
static Btn   b1, b2;
static Scr   scr     = SM;
static DS    dst     = SI;
static int16_t  stmp = TDEF, ctmp = TDEF;
static uint16_t stim = MDEF, ctim = MDEF;
static uint32_t rsec = 0;
static int16_t  atemp = 0, htemp = 0;
static uint8_t  frd  = 1;
static uint32_t tb = 0, tt = 0, ts = 0, td = 0;

/* Shadow state – only redraw fields whose value changed */
static int16_t  p_at = -1;
static uint16_t p_rm = 0xFFFF;
static int16_t  p_st = -1;
static DS       p_ds = SI;

static uint8_t  dir  = 0;   /* 0 = UP, 1 = DOWN */

/* ══════════════════════════════════════════════════════════════════
 *  ST7735  LOW-LEVEL
 * ══════════════════════════════════════════════════════════════════ */
static void Sel(void)   { HAL_GPIO_WritePin(ST7735_CS_GPIO_Port, ST7735_CS_Pin, GPIO_PIN_RESET); }
static void Unsel(void) { HAL_GPIO_WritePin(ST7735_CS_GPIO_Port, ST7735_CS_Pin, GPIO_PIN_SET); }

static void Wr(uint8_t d, uint8_t dc)
{
    HAL_GPIO_WritePin(ST7735_DC_GPIO_Port, ST7735_DC_Pin,
                      dc ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &d, 1, HAL_MAX_DELAY);
}

#define SPIBUF 128
static uint8_t sbuf[SPIBUF];

static void Win(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    Wr(0x2A,0); Wr(0,1); Wr(x0+XOFF,1); Wr(0,1); Wr(x1+XOFF,1);
    Wr(0x2B,0); Wr(0,1); Wr(y0+YOFF,1); Wr(0,1); Wr(y1+YOFF,1);
    Wr(0x2C,0);
}

static void Rst(void)
{
    HAL_GPIO_WritePin(ST7735_RES_GPIO_Port, ST7735_RES_Pin, GPIO_PIN_RESET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(ST7735_RES_GPIO_Port, ST7735_RES_Pin, GPIO_PIN_SET);
}

/* ── Init tables ────────────────────────────────────────────────── */
#define DF 0x80

static const uint8_t ic1[] = {
    15,
    0x01,DF,150, 0x11,DF,255,
    0xB1,3,0x01,0x2C,0x2D,
    0xB2,3,0x01,0x2C,0x2D,
    0xB3,6,0x01,0x2C,0x2D,0x01,0x2C,0x2D,
    0xB4,1,0x07,
    0xC0,3,0xA2,0x02,0x84,
    0xC1,1,0xC5,
    0xC2,2,0x0A,0x00,
    0xC3,2,0x8A,0x2A,
    0xC4,2,0x8A,0xEE,
    0xC5,1,0x0E,
    0x20,0,
    0x36,1,0xA0,
    0x3A,1,0x05,
};
static const uint8_t ic2[] = {
    2, 0x2A,4,0,0,0,0x9F, 0x2B,4,0,0,0,0x7F,
};
static const uint8_t ic3[] = {
    4,
    0xE0,16, 0x02,0x1C,0x07,0x12,0x37,0x32,0x29,0x2D,
             0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10,
    0xE1,16, 0x03,0x1D,0x07,0x06,0x2E,0x2C,0x29,0x2D,
             0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10,
    0x13,DF,10,
    0x29,DF,100,
};

static void ExCmd(const uint8_t *a)
{
    uint8_t n = *a++;
    while (n--) {
        Wr(*a++, 0);
        uint8_t ar = *a++;
        uint8_t dl = ar & DF;
        ar &= (uint8_t)~DF;
        while (ar--) Wr(*a++, 1);
        if (dl) { uint16_t ms = *a++; if (ms == 255) ms = 500; HAL_Delay(ms); }
    }
}

static void DispInit(void)
{
    Sel(); Rst();
    ExCmd(ic1); ExCmd(ic2); ExCmd(ic3);
    Unsel();
}

/* ══════════════════════════════════════════════════════════════════
 *  GRAPHICS
 * ══════════════════════════════════════════════════════════════════ */
static void Fill(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t c)
{
    Sel();
    Win(x, y, (uint8_t)(x+w-1), (uint8_t)(y+h-1));
    uint8_t hi = (uint8_t)(c>>8), lo = (uint8_t)c;
    for (uint16_t i = 0; i < SPIBUF; i += 2) { sbuf[i] = hi; sbuf[i+1] = lo; }
    HAL_GPIO_WritePin(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_SET);
    uint32_t total = (uint32_t)w * h * 2U;
    while (total > 0) {
        uint16_t ck = (total > SPIBUF) ? SPIBUF : (uint16_t)total;
        HAL_SPI_Transmit(&hspi1, sbuf, ck, HAL_MAX_DELAY);
        total -= ck;
    }
    Unsel();
}

static void Clr(void) { Fill(0,0,SW,SH,C_BK); }

static void DC(uint8_t x, uint8_t y, char c, uint16_t fg, uint16_t bg, uint8_t s)
{
    const uint8_t *gl = GF[(uint8_t)gidx(c)];
    uint8_t fh=(uint8_t)(fg>>8), fl=(uint8_t)fg;
    uint8_t bh=(uint8_t)(bg>>8), bl=(uint8_t)bg;
    Sel();
    Win(x, y, (uint8_t)(x+6*s-1), (uint8_t)(y+7*s-1));
    HAL_GPIO_WritePin(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_SET);
    for (uint8_t r = 0; r < 7; r++) {
        uint16_t p = 0;
        for (uint8_t col = 0; col < 6; col++) {
            uint8_t on = (col < 5 && (gl[col] & (1U<<r))) ? 1 : 0;
            uint8_t ph = on ? fh : bh, pl = on ? fl : bl;
            for (uint8_t sx = 0; sx < s; sx++) { sbuf[p++] = ph; sbuf[p++] = pl; }
        }
        for (uint8_t sy = 0; sy < s; sy++)
            HAL_SPI_Transmit(&hspi1, sbuf, p, HAL_MAX_DELAY);
    }
    Unsel();
}

static void DT(uint8_t x, uint8_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t sc)
{
    while (*s) { DC(x,y,*s++,fg,bg,sc); x += 6*sc; }
}

/* ══════════════════════════════════════════════════════════════════
 *  NTC  –  integer-only lookup table
 *
 *  100 K NTC, B=3950, 100 K series, 12-bit ADC, 3.3 V
 *  Table: ADC value at 0,10,20,...,100 C  (descending ADC)
 * ══════════════════════════════════════════════════════════════════ */
static const uint16_t ntc[] = {
    3157, 2738, 2278, 1825, 1420, 1081, 814, 611, 460, 349, 268
};

static int16_t NtcLookup(uint16_t v)
{
    if (v >= ntc[0])  return 0;
    if (v <= ntc[10]) return 100;
    for (uint8_t i = 0; i < 10; i++) {
        if (v > ntc[i+1]) {
            int32_t span = (int32_t)ntc[i] - (int32_t)ntc[i+1];
            int32_t pos  = (int32_t)ntc[i] - (int32_t)v;
            return (int16_t)(i * 10 + (int16_t)(pos * 10 / span));
        }
    }
    return 0;
}

static int16_t ReadNTC(uint32_t ch)
{
    ADC_ChannelConfTypeDef sc = {0};
    sc.Channel = ch;
    sc.Rank    = ADC_RANK_CHANNEL_NUMBER;
    if (HAL_ADC_ConfigChannel(&hadc1, &sc) != HAL_OK) return -1;
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 100) != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return -1;
    }
    uint16_t v = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return NtcLookup(v);
}

/* ══════════════════════════════════════════════════════════════════
 *  BUTTONS
 * ══════════════════════════════════════════════════════════════════ */
static void BInit(Btn *b, GPIO_TypeDef *p, uint16_t n)
{
    b->port=p; b->pin=n;
    b->pr=0; b->lr=0; b->dt=0; b->pt=0; b->lf=0;
}

static BE BUpd(Btn *b)
{
    uint8_t raw = (HAL_GPIO_ReadPin(b->port, b->pin) == GPIO_PIN_RESET) ? 1U : 0U;
    uint32_t now = HAL_GetTick();
    if (raw != b->lr) { b->dt = now; b->lr = raw; }
    if ((now - b->dt) < DEB) return BN;
    if (raw && !b->pr)  { b->pr=1; b->pt=now; b->lf=0; }
    else if (!raw && b->pr) { b->pr=0; return b->lf ? BN : BS; }
    else if (raw && b->pr && !b->lf && (now-b->pt)>=LNG) { b->lf=1; return BL; }
    return BN;
}

/* ══════════════════════════════════════════════════════════════════
 *  HEATER  /  FAN
 * ══════════════════════════════════════════════════════════════════ */
static void HSet(uint16_t d) { __HAL_TIM_SET_COMPARE(&htim1,  TIM_CHANNEL_1, d); }
static void FSet(uint16_t d) { __HAL_TIM_SET_COMPARE(&htim17, TIM_CHANNEL_1, d); }

static void Regulate(void)
{
    if (dst != SR) {
        HSet(0); FSet(0);
        HAL_GPIO_WritePin(led_GPIO_Port, led_Pin, GPIO_PIN_RESET);
        return;
    }
    if (htemp > TSAFE) { HSet(0); FSet(65535); return; }
    int16_t e = stmp - atemp;
    if      (e > 10) HSet(65535);
    else if (e > 0)  HSet((uint16_t)((uint32_t)e * 6553U));
    else             HSet(0);
    FSet(65535);
    HAL_GPIO_WritePin(led_GPIO_Port, led_Pin, GPIO_PIN_SET);
}

/* ══════════════════════════════════════════════════════════════════
 *  FORMATTING
 * ══════════════════════════════════════════════════════════════════ */
static void FmtT(int16_t t, char *b)          /* "NN" */
{
    if (t < 0)  t = 0;
    if (t > 99) t = 99;
    b[0]='0'+(char)(t/10); b[1]='0'+(char)(t%10); b[2]=0;
}

static void FmtH(uint16_t m, char *b)         /* "HH:MM" */
{
    uint8_t h=(uint8_t)(m/60), mi=(uint8_t)(m%60);
    b[0]='0'+h/10; b[1]='0'+h%10;
    b[2]=':';
    b[3]='0'+mi/10; b[4]='0'+mi%10; b[5]=0;
}

/* ══════════════════════════════════════════════════════════════════
 *  SCREEN DRAWING
 * ══════════════════════════════════════════════════════════════════ */

/* ── MAIN ─────────────────────────────────────────────────────── */
static void DrMain(uint8_t f)
{
    char buf[10];

    if (f) {
        Clr();
        Fill(0, 36, SW, 1, C_DK);
        Fill(0, 80, SW, 1, C_DK);
    }

    /* Air temperature – only if changed */
    if (f || atemp != p_at) {
        p_at = atemp;
        FmtT(atemp, buf);
        DT(4, 4, buf, C_CY, C_BK, 4);
        buf[0]=CDEG; buf[1]='C'; buf[2]=0;
        DT(52, 12, buf, C_CY, C_BK, 2);
    }

    /* Time remaining – only if minute changed */
    {
        uint16_t rm = (dst != SI) ? (uint16_t)(rsec/60) : stim;
        if (f || rm != p_rm) {
            p_rm = rm;
            FmtH(rm, buf);
            DT(94, 12, buf, C_YL, C_BK, 2);
        }
    }

    /* State + hints – only if state changed */
    if (f || dst != p_ds) {
        p_ds = dst;
        uint16_t    sc;
        const char *sl;
        switch (dst) {
            case SR: sl = "RUN  "; sc = C_GN; break;
            case SP: sl = "PAUSE"; sc = C_OR; break;
            default: sl = "IDLE "; sc = C_GR; break;
        }
        DT(35, 42, sl, sc, C_BK, 3);

        const char *h;
        switch (dst) {
            case SR: h = "1:SET  2:PAUSE"; break;
            case SP: h = "1:SET  2:GO   "; break;
            default: h = "1:SET  2:RUN  "; break;
        }
        DT(38, 90, h,             C_DK, C_BK, 1);
        DT(50, 102, "HOLD2:STOP",  C_DK, C_BK, 1);
    }

    /* SET:NN°C – only if changed */
    if (f || stmp != p_st) {
        p_st = stmp;
        buf[0]='S'; buf[1]='E'; buf[2]='T'; buf[3]=':';
        FmtT(stmp, buf+4);
        buf[6]=CDEG; buf[7]='C'; buf[8]=0;
        DT(56, 68, buf, C_GR, C_BK, 1);
    }
}

/* ── SET TEMP ─────────────────────────────────────────────────── */
static void DrTemp(uint8_t f)
{
    char buf[4];

    if (f) {
        Clr();
        DT(32, 6, "SET TEMP",  C_WH, C_BK, 2);
        Fill(0, 24, SW, 1, C_DK);
        DT(4, 112, "HOLD2:CANCEL",   C_DK, C_BK, 1);
    }

    FmtT(ctmp, buf);
    DT(32, 42, buf, C_CY, C_BK, 4);
    buf[0]=CDEG; buf[1]='C'; buf[2]=0;
    DT(80, 50, buf, C_CY, C_BK, 2);
    DT(4, 100, dir ? "1:DN  2:GO  H1:UP" : "1:UP  2:GO  H1:DN", C_DK, C_BK, 1);
}

/* ── SET TIME ─────────────────────────────────────────────────── */
static void DrTime(uint8_t f)
{
    char buf[8];

    if (f) {
        Clr();
        DT(32, 6, "SET TIME",  C_WH, C_BK, 2);
        Fill(0, 24, SW, 1, C_DK);
        DT(4, 112, "HOLD2:CANCEL",   C_DK, C_BK, 1);
    }

    FmtH(ctim, buf);
    DT(22, 42, buf, C_YL, C_BK, 3);
    DT(112, 49, "H", C_GR, C_BK, 2);
    DT(4, 100, dir ? "1:DN  2:GO  H1:UP" : "1:UP  2:GO  H1:DN", C_DK, C_BK, 1);
}

static void DrCur(uint8_t f)
{
    switch (scr) {
        case SM: DrMain(f); break;
        case SC: DrTemp(f); break;
        case SD: DrTime(f); break;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  INPUT
 * ══════════════════════════════════════════════════════════════════ */
static void Input(BE e1, BE e2)
{
    if (e1 == BN && e2 == BN) return;

    switch (scr) {

    case SM:
        if (e1 == BS) {
            ctmp = stmp;
            scr  = SC;
            frd  = 1;
        }
        if (e2 == BS) {
            if (dst == SI) {
                dst  = SR;
                rsec = (uint32_t)stim * 60U;
                ts   = HAL_GetTick();
            } else if (dst == SR) {
                dst = SP;
                HSet(0);
            } else {
                dst = SR;
                ts  = HAL_GetTick();
            }
            frd = 1;
        }
        if (e2 == BL) {
            dst  = SI;
            stmp = TDEF;
            stim = MDEF;
            rsec = 0;
            HSet(0); FSet(0);
            HAL_GPIO_WritePin(led_GPIO_Port, led_Pin, GPIO_PIN_RESET);
            frd = 1;
        }
        break;

    case SC:
        if (e1 == BS) {
            if (dir) { ctmp--; if (ctmp < TMIN) ctmp = TMAX; }
            else     { ctmp++; if (ctmp > TMAX) ctmp = TMIN; }
            DrTemp(0);
        }
        if (e1 == BL) { dir ^= 1; DrTemp(0); }
        if (e2 == BS) {
            stmp = ctmp;
            ctim = stim;
            dir  = 0;
            scr  = SD;
            frd  = 1;
        }
        if (e2 == BL) { dir = 0; scr = SM; frd = 1; }
        break;

    case SD:
        if (e1 == BS) {
            if (dir) { ctim = (ctim <= MMIN) ? MMAX : ctim - MSTP; }
            else     { ctim = (ctim >= MMAX) ? MMIN : ctim + MSTP; }
            DrTime(0);
        }
        if (e1 == BL) { dir ^= 1; DrTime(0); }
        if (e2 == BS) {
            stim = ctim;
            rsec = (uint32_t)stim * 60U;
            dst  = SR;
            ts   = HAL_GetTick();
            dir  = 0;
            scr  = SM;
            frd  = 1;
        }
        if (e2 == BL) { dir = 0; scr = SM; frd = 1; }
        break;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  PUBLIC API
 * ══════════════════════════════════════════════════════════════════ */
void Dryer_Init(void)
{
    DispInit();
    HAL_GPIO_WritePin(ST7735_BL_GPIO_Port, ST7735_BL_Pin, GPIO_PIN_SET);
    Clr();

    HAL_TIM_PWM_Start(&htim1,  TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim17, TIM_CHANNEL_1);
    HSet(0); FSet(0);

    BInit(&b1, b1_GPIO_Port, b1_Pin);
    BInit(&b2, b2_GPIO_Port, b2_Pin);

    tb = tt = ts = td = HAL_GetTick();
    frd = 1;
}

void Dryer_Loop(void)
{
    uint32_t now = HAL_GetTick();

    /* Buttons (every 20 ms) */
    if ((now - tb) >= 20U) {
        tb = now;
        Input(BUpd(&b1), BUpd(&b2));
    }

    /* Temperature (every 500 ms) */
    if ((now - tt) >= 500U) {
        tt = now;
        atemp = ReadNTC(ADC_CHANNEL_11);
        htemp = ReadNTC(ADC_CHANNEL_12);
    }

    /* 1-second countdown */
    if ((now - ts) >= 1000U) {
        ts = now;
        if (dst == SR && rsec > 0) {
            rsec--;
            if (rsec == 0) {
                dst = SI;
                HSet(0); FSet(0);
                HAL_GPIO_WritePin(led_GPIO_Port, led_Pin, GPIO_PIN_RESET);
                frd = 1;
            }
        }
    }

    Regulate();

    /* Display: full redraw on demand, dirty-check only on main screen */
    if (frd) {
        td = now;
        DrCur(1);
        frd = 0;
    } else if (scr == SM && (now - td) >= 250U) {
        td = now;
        DrMain(0);
    }
}
