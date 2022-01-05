#include "mbed.h"
#include "TextLCD.h"
#include "Keypad.h"  

#define UART_BR             9600
#define I2C_FREQ            100000    

#define EEPROM_SLAVE_ADR    0x50

#define M_KEY_SIZE          5
#define STATUS_SIZE         1
#define GENERATED_CODE_SIZE 13

#define WAIT_BEFORE_READ    10 //ms
#define READY               'R'
#define NOT_READY           'N'

#define MAIN_SCREEN         0
#define LOOP_SCREEN         1
#define ENTER_CODE_SCREEN   2
#define CHANGE_M_KEY_SCREEN 3
#define LOCKED_SCREEN       4

#define LOCKED              1
#define UNLOCKED            0
#define MAX_FAULT_NUMBER    3   

Keypad tipkovnica(D2,D3,D4,D5,D8,D9,D10,D11);                                   
TextLCD lcd(PTE20, PTE21, PTE22, PTE23, PTE29, PTE30, TextLCD::LCD16x2);   
Serial HC05(PTE0,PTE1);  
I2C eeprom(PTC9, PTC8);  
InterruptIn btn1(D2);  
InterruptIn btn2(D3);
Serial pc(USBTX, USBRX); 

void EEPROMread(const char* address, char* data, uint8_t size);
void writeKey(uint8_t digit_number, char* array);
bool scanEnterPressed();
void enterCode();
void enterMasterKey();
void buttonControl(bool state);
void caseSet(char* first_row, char* second_row, bool state, char case_state);
void getEEPROMData();
void rxInterrupt();
void init();

const char m_key_adr = 0x94;
const char status_adr = 0x80;

char runtime_entered_code[GENERATED_CODE_SIZE] = "",            
     rx_code[GENERATED_CODE_SIZE] = "123456789012",           
     master_key[M_KEY_SIZE] = "";        
              
char current_screen, keypad_value, lock = LOCKED;         
uint8_t screen_col;

bool if_button_pressed = 0;
                  
int main(){  
                         
    uint8_t fault_number = 0;

    init();
    getEEPROMData();
    
    while(true) {
        switch(current_screen) {
            
            case MAIN_SCREEN:
                tipkovnica.cetvrtiStupac(); //Provjeravanje 4. stupca
                caseSet("     Skener","      koda",1,1);
            break;
            
            case LOOP_SCREEN: 
            break;

            case ENTER_CODE_SCREEN:
                caseSet("Unesite kod:","",0,0);
                
                while(true) {
                    
                    keypad_value = tipkovnica.keyscan();
                    if(keypad_value == 'c') break;
                    writeKey(GENERATED_CODE_SIZE - 1, runtime_entered_code);

                    if(scanEnterPressed() && runtime_entered_code[0] != '\0') {
                                               
                        if(memcmp(rx_code, runtime_entered_code, GENERATED_CODE_SIZE - 1) == 0) {
                            lcd.cls();
                            lcd.printf("Tocno!");
                            memset(rx_code,0,GENERATED_CODE_SIZE - 1);
                            fault_number = 0;
                            wait(2);
                        } else {
                            fault_number++;
                            lcd.cls();
                            lcd.printf("Krivo!");
                            wait(2);
                        }
                        
                        if(fault_number == MAX_FAULT_NUMBER ){
                            fault_number = 0;
                            const char data_out [3] = {status_adr, LOCKED}
                            eeprom.write(EEPROM_SLAVE_ADR << 1,data_out,2,false);
                            current_screen = LOCKED_SCREEN;
                            HC05.putc(NOT_READY);
                        }
                        break;
                    }
                }
                break;

            case CHANGE_M_KEY_SCREEN:
                caseSet("Promjeni M.key:","",0,0);              
                while(true) {
                    
                    keypad_value = tipkovnica.keyscan();                    
                    if(keypad_value == 'c') break;
                    writeKey(M_KEY_SIZE - 1, runtime_entered_code);

                    if(scanEnterPressed() && runtime_entered_code[3] != '\0') {
                        memmove(master_key, runtime_entered_code, M_KEY_SIZE - 1);
                        
                        char data_out[6] = {m_key_adr};                     
                        for(uint8_t i = 0; i < M_KEY_SIZE - 1 ; i++) {
                            data_out[i+1] = master_key[i];
                        }
                        
                        eeprom.write(EEPROM_SLAVE_ADR << 1, data_out, M_KEY_SIZE, false);
                        wait_ms(WAIT_BEFORE_READ);
                        break;
                    }
                }
                break;
                
                case LOCKED_SCREEN:
                caseSet("Unesite M.key:","",0,4);
                
                while(true) {
                    
                    keypad_value = tipkovnica.keyscan(); 
                    
                    if(keypad_value == 'c'){
                        if(if_button_pressed){current_screen = MAIN_SCREEN; if_button_pressed = true; break;}
                    else
                    continue;
                    }
                    writeKey(M_KEY_SIZE - 1, runtime_entered_code);

                    if(scanEnterPressed() && runtime_entered_code[3] != '\0') {
                        if(memcmp(master_key, runtime_entered_code, M_KEY_SIZE - 1) == 0) {
                            if(if_button_pressed){current_screen = CHANGE_M_KEY_SCREEN; if_button_pressed = false; break;}
                                const char data_out [3] = {status_adr, UNLOCKED}
                                eeprom.write(EEPROM_SLAVE_ADR << 1, data_out, 2, false);
                                current_screen = MAIN_SCREEN;
                                HC05.putc(READY);
                        } else {
                           if(if_button_pressed){current_screen = MAIN_SCREEN; if_button_pressed = false; break;}
                           current_screen = LOCKED_SCREEN;
                           }
                    break;  
                    }
                }
                break;
                
                default:
                    current_screen = LOCKED_SCREEN;
                    break;
            }
    }
}

void EEPROMread(const char* address, char* data, uint8_t size){
    eeprom.write(EEPROM_SLAVE_ADR << 1, address, 1, true); //Postavljanje početne adrese za čitanje
    eeprom.read((EEPROM_SLAVE_ADR << 1)| 0x01 ,data ,size, false); //Čitanje podataka od postavljenje adrese i nadalje
}

void getEEPROMData(){   
    EEPROMread(&m_key_adr, master_key, M_KEY_SIZE - 1); 
    wait_ms(WAIT_BEFORE_READ);
    EEPROMread(&status_adr, &lock, STATUS_SIZE);
    
    if(lock == LOCKED){ 
        current_screen = LOCKED_SCREEN;
        HC05.putc(NOT_READY);
    
    }else{     
        current_screen = MAIN_SCREEN;
        HC05.putc(READY);
    }
}

void caseSet(char* first_row, char* second_row, bool state, char case_state){
    buttonControl(state);                       //Aktivacija il deaktivacija tipki
    memset(runtime_entered_code,0,GENERATED_CODE_SIZE - 1);     //Pražnjenej arraya "kod"
    
    lcd.cls();
    lcd.locate(0,0);
    lcd.printf("%s", first_row);
    lcd.locate(0,1);
    lcd.printf("%s", second_row);
    
    current_screen = case_state;
    screen_col = 0; 
}

void buttonControl(bool state){                   
    if(state){
        btn1.fall(&enterCode); //Tipka A
        btn2.fall(&enterMasterKey); //Tipka B          
    }else{
        btn1.fall(NULL);
        btn2.fall(NULL);
    }    
}

void writeKey(uint8_t digit_number, char* array){    
    if(keypad_value == '*' && screen_col > 0){ // 'a' znači brisanje jednog karaktera
        screen_col--;        
        lcd.locate(screen_col,1);
        lcd.printf(" ");
        array[screen_col] = 0;
        wait(0.2);
    }

    else if(keypad_value == '*' || keypad_value == '#' || keypad_value == 'c')
        return;
        
    else if(screen_col == broj_znamenki){
        lcd.locate(screen_col,1);
        lcd.printf(" ");
        wait(0.1);
    }else{
        lcd.locate(screen_col,1);
        lcd.printf("%c",keypad_value);
        array[screen_col] = keypad_value;
        screen_col++;
        wait(0.2);
    }                               
}

bool scanEnterPressed(){                                   
    return (keypad_value == 'b') * 1;      
}

void enterCode(){                                   
    current_screen = ENTER_CODE_SCREEN;
}

void enterMasterKey(){                                   
    current_screen = LOCKED_SCREEN;
    if_button_pressed = true;
}

void rxInterrupt(){
    char temp = HC05.getc();
        if(temp == '?'){
            if(current_screen != LOCKED_SCREEN || if_button_pressed == true)
            HC05.putc(READY);
            else
            HC05.putc(NOT_READY);
        }else{
            
    HC05.putc('?');
    
    for(uint8_t i = 0; i< GENERATED_CODE_SIZE - 1; i++)
    rx_code[i] = HC05.getc();
    } 
}

void init(){
    HC05.baud(UART_BR);
    HC05.attach(&rxInterrupt,Serial::RxIrq); 
    eeprom.frequency(I2C_FREQ );
}

