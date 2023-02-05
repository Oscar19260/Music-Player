#include <io.h>
#include <delay.h>
#include <display.h>
#include <stdio.h>
#include <ff.h>

#asm
    .equ __lcd_port=0x02
    .equ __lcd_EN=1
    .equ __lcd_RS=0
    .equ __lcd_D4=2
    .equ __lcd_D5=3
    .equ __lcd_D6=4
    .equ __lcd_D7=5
#endasm

//Código base que reproduce A001.WAV que es un WAV, Mono, 8-bit, y frec de muestreo de 22050HZ
// T reproducción = 1/22050 = 45.35 useg

//Timer 0: Fast PWM se usa para la DAC (Digital to Analog Converter)
//Timer 1: Interrupción periódica cada 10mseg necesaria para la SD
//Timer 2: CTC. Interrupción periódica cada periodo de reproducción (T rep) 45.35 useg
   
char bufferL[256];
char bufferH[256]; 
char NombreArchivo[]  = "0:A001.wav";
unsigned int i=0;
unsigned int j=0;
unsigned int k;
unsigned int aux=0;
bit LeerBufferH,LeerBufferL;
unsigned long muestras;
unsigned int song=0;
bit stereo;

interrupt [TIM1_COMPA] void timer1_compa_isr(void){
disk_timerproc();
/* MMC/SD/SD HC card access low level timing function */
}
        
//Interrupción que se ejecuta cada T=1/Fmuestreo_Wav
interrupt [TIM2_COMPA] void timer2_compa_isr(void){
    if (stereo==0){
        if (i<256){
            OCR0A=bufferL[i];
            OCR0B=bufferL[i++];
        }  
        else{
            OCR0A=bufferH[i-256]; 
            OCR0B=bufferH[i-256]; 
            i++;
        }   
        if (i==256)
            LeerBufferL=1;
        if (i==512){
            LeerBufferH=1;
            i=0;
        }
    }
      
    else{
        if (i<256){
            OCR0A=bufferL[i++];
            OCR0B=bufferL[i++]; 
        }  
        else{
            OCR0A=bufferH[i-256];
            i++;
            OCR0B=bufferH[i-256];
            i++;
        }   
        if (i==256)
            LeerBufferL=1;
        if (i==512){
            LeerBufferH=1;
            i=0;
        }
    }     
}

void main(){
    unsigned int  br;
       
    /* FAT function result */
    FRESULT res;            
   
    /* will hold the information for logical drive 0: */
    FATFS drive;
    FIL archivo; // file objects 
                                  
    CLKPR=0x80;         
    CLKPR=0x01;         //Cambiar a 8MHz la frecuencia de operación del micro 
       
    // Código para hacer una interrupción periódica cada 10ms
    // Timer/Counter 1 initialization
    // Clock source: System Clock
    // Clock value: 1000.000 kHz
    // Mode: CTC top=OCR1A
    // Compare A Match Interrupt: On
    TCCR1B=0x0A;     //CK/8 10ms con oscilador de 8MHz
    OCR1AH=0x27;
    OCR1AL=0x10;
    TIMSK1=0x02; 
                                                    
    //PWM para conversión Digital Analógica WAV->Sonido
    // Timer/Counter 0 initialization
    // Clock source: System Clock
    // Clock value: 8000.000 kHz
    // Mode: Fast PWM top=0xFF
    // OC0A output: Non-Inverted PWM
    TCCR0A=0xA3;         
    
    DDRB.7=1;  //Salida bocina (OC0A) 
    DDRG.4=1;  //Salida bocina (OC0B)
                                  
    // Timer/Counter 2 initialization
    // Clock source: System Clock
    // Clock value: 1000.000 kHz
    // Mode: CTC top=OCR2A
    ASSR=0x00;
    TCCR2A=0x02;
    TCCR2B=0x02;
    OCR2A=0x2C;         //Este valor va a cambiar por cada frecuencia de muestreo diferente que hay 
        
    // Timer/Counter 2 Interrupt(s) initialization
    TIMSK2=0x02;

    DDRD.7=1; 
    //Puertos para botones 
    PORTD.0=1;     //Play/Pause en Pull up
    PORTD.1=1;     //Rewind en Pull up
    PORTD.2=1;     //Forward en Pull up
    
    SetupLCD();
    #asm("sei")   
    disk_initialize(0);  /* Inicia el puerto SPI para la SD */
    delay_ms(500); 
    
    /* mount logical drive 0: */
    if ((res=f_mount(0,&drive))==FR_OK){  
        while(1){ 
            
            song++;
            if(song>=6){ 
                song=1;
            }
            NombreArchivo[5]= song+'0';
            
            /*Lectura de Archivo*/
            res = f_open(&archivo, NombreArchivo, FA_OPEN_EXISTING | FA_READ);
            if (res==FR_OK){ 
                PORTD.7=1;
                
                //Leer propiedades de la canción
                f_lseek(&archivo, 0);
                f_read(&archivo, bufferL, 44,&br); //leer encabezado 
                
                //Procesar si es mono o estereo
                if(bufferL[22]==1)
                    stereo=0;
                else
                    stereo=1; 
                i=0;   
                //Obtenemos frecuencia de muestreo para calcular el valor de OCR2A
                //Comparar bufferL[24] y/o bufferL[25]   
                muestras = (long)bufferL[43]*16777216 + (long)bufferL[42]*65536 + (long)bufferL[41]*256 + (long)bufferL[40];
                if (bufferL[24] == 0x80)        //Frecuencia = 16,000  Trep = 62.5useg = 62 
                    OCR2A = 62;   
                else if (bufferL[24] == 0x22)   //Frecuencia = 22,050  Trep = 45.3useg = 45
                    OCR2A = 45; 
                else if (bufferL[24] == 0xC0)   //Frecuencia = 24,000  Trep = 41.6 = 42
                    OCR2A = 42; 
                f_lseek(&archivo, muestras+44);
                f_read(&archivo, bufferH, 100,&br); //En bufferH esta la info de la canción  
                f_lseek(&archivo, muestras+44);
                f_read(&archivo, bufferH, 100,&br); //En bufferH esta la info de la canción  
                
                //Imprime canción  
                aux=0; 
                for(j=20; j<100; j++){
                    MoveCursor(aux,0);
                    CharLCD(bufferH[j]);
                    if(bufferH[j] == 0){ 
                        k=j;
                        break;
                    }
                    aux++;
                }  
                MoveCursor(aux,0); 
                CharLCD(' ');
                
                for(j=k; j<100; j++){
                    if(bufferH[j] == 0x54){ 
                        k=j+5;
                        break; 
                    }
                }
                
                aux=0;
                for(j=k; j<100; j++){
                    MoveCursor(aux,1);  
                    CharLCD(bufferH[j]);
                    if(bufferH[j] == 0) 
                        break;
                    aux++;
                }
                MoveCursor(aux,1); 
                CharLCD(' ');
                                      
                f_lseek(&archivo, 44);   
                
                f_read(&archivo, bufferL, 256,&br); //leer los primeros 512 bytes del WAV
                f_read(&archivo, bufferH, 256,&br);    
                LeerBufferL=0;     
                LeerBufferH=0;
                TCCR0B=0x01;    //Prende sonido
                do{   
                     while((LeerBufferH==0)&&(LeerBufferL==0));
                     if (LeerBufferL){                       
                         f_read(&archivo, bufferL, 256,&br); //leer encabezado  
                         LeerBufferL=0;  
                     }
                     else{ 
                         f_read(&archivo, bufferH, 256,&br); //leer encabezado
                         LeerBufferH=0;
                        
                     }       
                     //Código para estatus switches
                     //Star/Stop
                     if(PIND.0==0){
                        delay_ms(30);
                        while(PIND.0 == 0);
                        delay_ms(10); 
                        if(TCCR0B == 0x00)
                            TCCR0B = 0x01;
                        else
                            TCCR0B = 0x00;
                        while((PIND.0==1) && (TCCR0B == 0x00)|);                       
                     } 
                     
                     //Rewind
                     if (PIND.1==0){
                        delay_ms(30);
                        f_lseek(&archivo, 44);
                        f_read(&archivo, bufferL, 256,&br);
                        f_read(&archivo, bufferH, 256,&br);    
                        LeerBufferL=0;     
                        LeerBufferH=0;
                        i=0;
                        while(PIND.1==0);
                        delay_ms(10);     
                     }
                     
                     // Forward
                     if(PIND.2 == 0){
                        delay_ms(30);
                        br=0;
                        while(PIND.2 == 0);
                        delay_ms(10);
                     }
                              
                }while(br==256); 
                TCCR0B=0x00;   //Apaga sonido  
                EraseLCD();
                f_close(&archivo); 
            }              
        }
    }
    f_mount(0, 0); //Cerrar drive de SD
    while(1);
}
