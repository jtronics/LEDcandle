//#######################################################################################################
// autor  	: Martin Junghans
// Version	: 1.2
// Datum	: Dezember 2009
//#######################################################################################################

#ifndef F_CPU
#define F_CPU         8000000L           // Systemtakt in Hz
#endif
#define F_PWM         120L               // PWM-Frequenz in Hz
#define PWM_PRESCALER 8					// Vorteiler für den Timer
#define PWM_STEPS     256                // PWM-Schritte pro Zyklus(1..256)
#define PWM_PORT      PORTB              // Port für PWM
#define PWM_DDR       DDRB               // Datenrichtungsregister für PWM
#define PWM_CHANNELS  8                  // Anzahl der PWM-Kanäle
 
// ab hier nichts ändern, wird alles berechnet
 
#define T_PWM (F_CPU/(PWM_PRESCALER*F_PWM*PWM_STEPS)) // Systemtakte pro PWM-Takt
//#define T_PWM 1   //TEST
 
#if ((T_PWM*PWM_PRESCALER)<(111+5))
    #error T_PWM zu klein, F_CPU muss vergrösst werden oder F_PWM oder PWM_STEPS verkleinert werden
#endif
 
#if ((T_PWM*PWM_STEPS)>65535)
    #error Periodendauer der PWM zu gross! F_PWM oder PWM_PRESCALER erhöhen.   
#endif
// includes
 
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h> 			// Watchdog

 
// globale Variablen
 
uint16_t pwm_timing[PWM_CHANNELS+1];          // Zeitdifferenzen der PWM Werte
uint16_t pwm_timing_tmp[PWM_CHANNELS+1];      
uint8_t  pwm_mask[PWM_CHANNELS+1];            // Bitmaske für PWM Bits, welche gelöscht werden sollen
uint8_t  pwm_mask_tmp[PWM_CHANNELS+1];        
uint8_t  pwm_setting[PWM_CHANNELS];           // Einstellungen für die einzelnen PWM-Kanäle
uint8_t  pwm_setting_tmp[PWM_CHANNELS+1];     // Einstellungen der PWM Werte, sortiert
 
volatile uint8_t pwm_cnt_max=1;               // Zählergrenze, Initialisierung mit 1 ist wichtig!
volatile uint8_t pwm_sync;                    // Update jetzt möglich
 
// Pointer für wechselseitigen Datenzugriff
 
uint16_t * isr_ptr_time  = pwm_timing;
uint16_t * main_ptr_time = pwm_timing_tmp;
uint8_t *  isr_ptr_mask  = pwm_mask;
uint8_t *  main_ptr_mask = pwm_mask_tmp;
 
//####################################################################################################### Zeiger austauschen
// das muss in einem Unterprogramm erfolgen,um eine Zwischenspeicherung durch den Compiler zu verhindern
void tausche_zeiger(void) 
	{
    uint16_t * tmp_ptr16;
    uint8_t * tmp_ptr8;
 
    tmp_ptr16 		= isr_ptr_time;
    isr_ptr_time 	= main_ptr_time;
    main_ptr_time 	= tmp_ptr16;
    tmp_ptr8 		= isr_ptr_mask;
    isr_ptr_mask 	= main_ptr_mask;
    main_ptr_mask 	= tmp_ptr8;
	}
 
//####################################################################################################### 
void pwm_update(void) // PWM Update, berechnet aus den PWM Einstellungen die neuen Werte für die Interruptroutine
{
    uint8_t i, j, k, min;
    uint8_t tmp;
 
    // PWM Maske für Start berechnen und gleichzeitig die Bitmasken generieren und PWM Werte kopieren
 
    tmp=0;
    j = 1;
    for(i=1; i<=(PWM_CHANNELS); i++) 
		{
        main_ptr_mask[i]=~j;                        // Maske zum Löschen der PWM Ausgänge
        pwm_setting_tmp[i] = pwm_setting[i-1];
        if (pwm_setting_tmp[i]!=0) tmp |= j;        // Maske zum setzen der IOs am PWM Start
        j <<= 1;
		}	
    main_ptr_mask[0]=tmp;                           // PWM Start Daten 
 

    // PWM settings sortieren; Einfügesortieren
 
    for(i=1; i<=PWM_CHANNELS; i++) 
		{
        min=255;
        k=i;
        for(j=i; j<=PWM_CHANNELS; j++) 
			{
            if (pwm_setting_tmp[j]<min) 
				{
                k=j;                                // Index und PWM-setting merken
                min = pwm_setting_tmp[j];
				}
			}
        if (k!=i) 
			{
            // ermitteltes Minimum mit aktueller Sortiertstelle tauschen
            tmp = pwm_setting_tmp[k];
            pwm_setting_tmp[k] = pwm_setting_tmp[i];
            pwm_setting_tmp[i] = tmp;
            tmp = main_ptr_mask[k];
            main_ptr_mask[k] = main_ptr_mask[i];
            main_ptr_mask[i] = tmp;
			}
		}
 
    // Gleiche PWM-Werte vereinigen, ebenso den PWM-Wert 0 löschen falls vorhanden
 
    k=PWM_CHANNELS;             // PWM_CHANNELS Datensätze
    i=1;                        // Startindex
 
    while(k>i) 
		{
        while ( ((pwm_setting_tmp[i]==pwm_setting_tmp[i+1]) || (pwm_setting_tmp[i]==0))  && (k>i) ) 
			{
 
            // aufeinanderfolgende Werte sind gleich und können vereinigt werden
            // oder PWM Wert ist Null
            if (pwm_setting_tmp[i]!=0)
                main_ptr_mask[i+1] &= main_ptr_mask[i];        // Masken vereinigen
 
            // Datensatz entfernen,
            // Nachfolger alle eine Stufe hochschieben
            for(j=i; j<k; j++) 
				{
                pwm_setting_tmp[j] = pwm_setting_tmp[j+1];
                main_ptr_mask[j] = main_ptr_mask[j+1];
				}
            k--;
			}
        i++;
		}
    
    // letzten Datensatz extra behandeln
    // Vergleich mit dem Nachfolger nicht möglich, nur löschen
    // gilt nur im Sonderfall, wenn alle Kanäle 0 sind
    if (pwm_setting_tmp[i]==0) k--;
 
    // Zeitdifferenzen berechnen
    
    if (k==0) { // Sonderfall, wenn alle Kanäle 0 sind
        main_ptr_time[0]=(uint16_t)T_PWM*PWM_STEPS/2;
        main_ptr_time[1]=(uint16_t)T_PWM*PWM_STEPS/2;
        k=1;
    }
    else 
		{
        i=k;
        main_ptr_time[i]=(uint16_t)T_PWM*(PWM_STEPS-pwm_setting_tmp[i]);
        j=pwm_setting_tmp[i];
        i--;
        for (; i>0; i--)
			{
            main_ptr_time[i]=(uint16_t)T_PWM*(j-pwm_setting_tmp[i]);
            j=pwm_setting_tmp[i];
			}
        main_ptr_time[0]=(uint16_t)T_PWM*j;
		}
 
    // auf Sync warten
 
    pwm_sync=0;             // Sync wird im Interrupt gesetzt
    while(pwm_sync==0);
 
    // Zeiger tauschen
    cli();
    tausche_zeiger();
    pwm_cnt_max = k;
    sei();
}
 
//####################################################################################################### Timer 1 Output COMPARE A Interrupt
ISR(TIMER1_COMPA_vect)
{
    static uint8_t pwm_cnt;
    uint8_t tmp;
 
    OCR1A += isr_ptr_time[pwm_cnt];
    tmp    = isr_ptr_mask[pwm_cnt];
    
    if (pwm_cnt == 0) 
		{
        PWM_PORT = tmp;                         // Ports setzen zu Begin der PWM
        pwm_cnt++;
		}
    else 
		{
        PWM_PORT &= tmp;                        // Ports löschen
        if (pwm_cnt == pwm_cnt_max) 
			{
            pwm_sync = 1;                       // Update jetzt möglich
            pwm_cnt  = 0;
			}
        else pwm_cnt++;
		}

	//TASTER ... AN oder AUS ... schaltet pin schnell wieder aus  wenn taster gedrückt...

 	if (!(PIND & (1<<PIND5)))	{PORTB &=~(1<<0);} 
 	if (!(PIND & (1<<PIND4)))	{PORTB &=~(1<<1);} 
 	if (!(PIND & (1<<PIND3)))	{PORTB &=~(1<<2);} 
	if (!(PIND & (1<<PIND2)))	{PORTB &=~(1<<3);} 
 	if (!(PINA & (1<<PINA0)))	{PORTB &=~(1<<4);} 
 	if (!(PINA & (1<<PINA1)))	{PORTB &=~(1<<5);} 
 	if (!(PIND & (1<<PIND1)))	{PORTB &=~(1<<6);} 
 	if (!(PIND & (1<<PIND0)))	{PORTB &=~(1<<7);} 

}

//#######################################################################################################
int main(void)
{
	DDRD	=0b00000000;	DDRA	=0b00000000;
	PORTD	=0b11111111;	PORTA	=0b11111111;
	
    PWM_DDR = 0xFF;         // Port als Ausgang
    
    // Timer 1 OCRA1, als variablem Timer nutzen
    TCCR1B = 2;             // Timer läuft mit Prescaler 8
    TIMSK |= (1<<OCIE1A);   // Interrupt freischalten


//###################### Watchdog RESET 
				/*
				Konstante 	Wert 	TimeOut
				WDTO_15MS 	0 		15 	ms
				WDTO_30MS 	1 		30	ms
				WDTO_60MS 	2 		60 	ms
				WDTO_120MS 	3 		120 ms
				WDTO_250MS 	4 		250 ms
				WDTO_500MS	5 		500 s
				WDTO_1S		6 		1 	S
				WDTO_2S 	7 		2	s		*/

	wdt_enable(WDTO_120MS);	
	
    sei();                  // Interrupts gloabl einschalten

	uint8_t zufall[8];
	uint8_t i;

	while (1) 
		{
		//### Watchdog
		wdt_reset();
		
		//### 
		for (i=0; i<PWM_CHANNELS; i++) 
			{
			if (zufall[i] > rand()%250) 	{zufall[i] -= rand()%40;}			//
			else 							{zufall[i] += rand()%30;}			
			}
		memcpy(pwm_setting, zufall, 8);
		
		
		pwm_update();
		};

    return 0;
}
