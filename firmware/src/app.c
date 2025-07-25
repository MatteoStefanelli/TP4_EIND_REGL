/*******************************************************************************
  MPLAB Harmony Application Source File
 *******************************************************************************/

#include "app.h"
#include "system_config.h"
#include "system_definitions.h"
#include <math.h>

// *****************************************************************************
// Section: Global Data Definitions
// *****************************************************************************

APP_DATA appData; // Structure globale d?�tat de l'application

// *****************************************************************************
// Section: Application Initialization and State Machine
// *****************************************************************************

void APP_Initialize(void) {
    appData.state = APP_STATE_INIT;
}

void APP_Tasks(void) {
    switch (appData.state) {
        case APP_STATE_INIT:
        {
            // Extinction des LEDs au d�marrage
            RED_LEDOff();
            YELLOW_LEDOff();
            GREEN_LEDOff();
            BLUE_LEDOff();
            
            // D�marrage des modules PWM et timers
            DRV_OC0_Start(); // PWM OC0
            DRV_TMR1_Start(); // Timer1 (r�gulation)
            DRV_TMR0_Start(); // Timer0 (autres t�ches)

            appData.state = APP_STATE_WAIT;
            break;
        }

        case APP_STATE_WAIT:
        {
            // Pas d'action sp�cifique ici
            break;
        }

        case APP_STATE_SERVICE_TASKS:
        {

            // � utiliser pour d'autres traitements si besoin
            break;
        }

        default:
        {
            break;
        }
    }
}

// Changement manuel d?�tat de la machine d'�tat

void APP_UpdateState(APP_STATES Newstate) {
    appData.state = Newstate;
}

// *****************************************************************************
// Section: R�gulation num�rique avec protection
// *****************************************************************************

// === PARAM�TRES DU SYST�ME ===
#define VREF            3.3f       // R�f�rence ADC
#define ADC_MAX         1023.0f    // R�solution 10 bits
#define TARGET_V        5.0f       // Tension cible de sortie (en V)

// === CONSTANTES PID ===
#define KP              1.0f       // Gain proportionnel
#define KI              40.0f      // Gain int�gral
#define DT              0.0001f    // P�riode d'�chantillonnage (100 �s)

// === GAINS HARDWARE ===
#define SHUNT_GAIN      21.0f      // Gain ampli courant
#define VOUT_GAIN       3.06f      // Ratio diviseur tension

// === LIMITES S�CURIT� (hardcod�es) ===
#define MAX_VOUT        5.5f       // Tension max (V)
#define MAX_IOUT        4.8f       // Courant max (A)

static float integrale = 0.0f; // Terme int�gral du r�gulateur
static bool faultState = false; // Drapeau d'erreur

// R�glage du PWM entre 0.0 et 1.0 (rapport cyclique)

void SetPWM(float duty) {
    // Saturation logique entre 0 et 1
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    const uint32_t period = 59999; // Timer2 r�gl� dans Harmony
    uint32_t compare = (uint32_t) (duty * period); // Valeur de compare


    DRV_OC0_PulseWidthSet(compare); // Application PWM
}

// Lecture tension de sortie (via ADC sur AN11)

float ReadVout(void) {
    uint16_t adcVal = DRV_ADC_SamplesRead(1); // AN11
    float vin = (adcVal * VREF / ADC_MAX); // Conversion ADC -> tension
    return vin * VOUT_GAIN; // Application du gain (diviseur)
}

// Lecture courant de sortie (via ADC sur AN12)

float ReadIout(void) {
    uint16_t adcVal = DRV_ADC_SamplesRead(0); // AN12
    float vshunt = (adcVal * VREF / ADC_MAX); // Tension sur shunt
    return vshunt / SHUNT_GAIN; // Application gain shunt
}

// V�rification des seuils s�curit� (tension + courant)

bool CheckSafety(void) {
    float vout = ReadVout();
    float iout = ReadIout();

    if (vout > MAX_VOUT || iout > MAX_IOUT) {
        RED_LEDOn(); // Indiquer l'erreur
        SetPWM(0.0f); // Couper le PWM
        faultState = true; // Basculer en erreur
        return false;
    }
    return true;
}

// Si l'erreur est pass�e, red�marrer la r�gulation

void SafeRecovery(void) {
    if (faultState) {
        float vout = ReadVout();
        if (vout < TARGET_V * 0.95f) { // Tension redevenue "safe"
            faultState = false;
            integrale = 0.0f;
            SetPWM(0.1f); // Reprise progressive
            RED_LEDOff(); // �teindre alarme
        }
    }
}

// Fonction principale de r�gulation (appel�e par timer1)

void PI_Regulation(void) {
    if (faultState) {
        SafeRecovery(); // Essayer recovery si en erreur
        return;
    }

    if (!CheckSafety()) return; // Blocage si hors-s�curit�

    float Vout = ReadVout();
    float error = TARGET_V - Vout;

    integrale += error * DT; // Accumuler erreur pour le I

    float output = KP * error + KI * integrale;

    // Saturation de la sortie (entre 0 et 1)
    if (output > 1.0f) output = 1.0f;
    if (output < 0.0f) output = 0.0f;

    SetPWM(output); // Appliquer le PWM r�gul�
}

// Callback appel� par le timer1 (toutes les 100 �s)

void App_Timer1Callback() {

    PI_Regulation();
}

/*************************************************/
/*************************************************/
/*************************************************/
/*****   fonction de transfert en Z de PID   *****/

// D�claration des constantes du filtre
#define a1  2.13
#define a2 -1.83
#define b0  0.24
#define b1 -0.24
// D�claration des variables statiques pour m�moriser l'�tat du filtre
static float x_k_1 = 0.0f; // x(k-1)
static float y_k_1 = 0.0f; // y(k-1)
static float y_k_2 = 0.0f; // y(k-2)
// Fonction du filtre � appeler � chaque nouvel �chantillon

float PIDMine(float x_k) {
    float y_k;
    // Calcul du filtre
    y_k = a1 * y_k_1 + a2 * y_k_2 + b0 * x_k + b1 * x_k_1;
    // Mise � jour des anciennes valeurs pour la prochaine it�ration
    x_k_1 = x_k;
    y_k_2 = y_k_1;
    y_k_1 = y_k;
    // Retourner la sortie courante
    return y_k;

}
/*******************************************************************************
 End of File
 *******************************************************************************/
