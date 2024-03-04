#include <Arduino.h>

#include <LiquidCrystal.h>

#define NOTE_B3  247
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494

#define BUTTON_OK 8
#define BUTTON_CANCEL 10
#define BUTTON_PREV 11
#define BUTTON_NEXT 12
#define melodyPin 13
#define TP36_SENSOR_CHANNEL 0
#define ADC_REF_VOLTAGE 5.0
#define HEATER_PIN 9

int wish_melody[] = {
  NOTE_B3, 
  NOTE_F4, NOTE_F4, NOTE_G4, NOTE_F4, NOTE_E4,
  NOTE_D4, NOTE_D4, NOTE_D4,
  NOTE_G4, NOTE_G4, NOTE_A4, NOTE_G4, NOTE_F4,
  NOTE_E4, NOTE_E4, NOTE_E4,
  NOTE_A4, NOTE_A4, NOTE_B4, NOTE_A4, NOTE_G4,
  NOTE_F4, NOTE_D4, NOTE_B3, NOTE_B3,
  NOTE_D4, NOTE_G4, NOTE_E4,
  NOTE_F4
};

int wish_tempo[] = {
  4,
  4, 8, 8, 8, 8,
  4, 4, 4,
  4, 8, 8, 8, 8,
  4, 4, 4,
  4, 8, 8, 8, 8,
  4, 4, 8, 8,
  4, 4, 4,
  2
};

enum Buttons
{
  EV_OK,
  EV_CANCEL,
  EV_NEXT,
  EV_PREV,
  EV_NONE,
  EV_MAX_NUM
};

enum Menus
{
  MENU_MAIN,
  MENU_KP,
  MENU_KD,
  MENU_KI,
  MENU_Tset,
  MENU_tracire,
  MENU_tmentinere,
  MENU_tincalzire,
  MENU_Start,
  MENU_MAX_NUM
};

enum SystemState
{
  SYSTEM_DONE,
  SYSTEM_PENDING,
  SYSTEM_ERROR,
  SYSTEM_START,
  SYSTEM_MAX_NUM
};

LiquidCrystal lcd(7, 6, 5, 4, 3, 2);
int curentTime;
int moving_setPoint;
int schema;
int temperatura = 45;
double kp = 8.0;
double kd = 1.2;
double ki = 0.6;
int timpMentinereSecunde = 30;
int timpMentinereMinute = 1;
int timpIncalzireSecunde = 30;
int timpIncalzireMinute = 1;
int timpRacireSecunde = 30;
int timpRacireMinute = 1;
static int suma_erori;
static int eroare_anterioara;
unsigned long lastButtonPressMillis = 0;
const unsigned long debounceDelay = 300;
bool melodie=true;
bool start=false;
Menus scroll_menu = MENU_MAIN;
Menus current_menu = MENU_MAIN;
SystemState systemState = SYSTEM_DONE;

void state_machine(enum Menus menu, enum Buttons button);
Buttons GetButtons(void);
void print_menu(enum Menus menu);
void enter_menu(void);
void go_home(void);
void go_next(void);
void go_prev(void);
void inc_kp(void);
void dec_kp(void);
void inc_kd(void);
void dec_kd(void);
void inc_ki(void);
void dec_ki(void);
void inc_temperatura(void);
void dec_temperatura(void);
void inc_timpRacire(void);
void dec_timpRacire(void);
void inc_timpMentinere(void);
void dec_timpMentinere(void);
void inc_timpIncalzire(void);
void dec_timpIncalzire(void);
void startPID(void);

typedef void(state_machine_handler_t)(void);

state_machine_handler_t *sm[MENU_MAX_NUM][EV_MAX_NUM] =
    {
        // EV_OK	 EV_CANCEL	EV_NEXT  EV_PREV
        {enter_menu, go_home, go_next, go_prev},                  // MENU_MAIN
        {go_home, go_home, inc_kp, dec_kp},                       // MENU_KP
        {go_home, go_home, inc_kd, dec_kd},                       // MENU_KD
        {go_home, go_home, inc_ki, dec_ki},                       // MENU_KI
        {go_home, go_home, inc_temperatura, dec_temperatura},     // MENU_Tset
        {go_home, go_home, inc_timpRacire, dec_timpRacire},       // MENU_tracire
        {go_home, go_home, inc_timpMentinere, dec_timpMentinere}, // MENU_tmentinere
        {go_home, go_home, inc_timpIncalzire, dec_timpIncalzire}, // MENU_tincalzire
        {startPID, go_home, go_home, go_home},                    // MENU_Start
};

void afisare_temp()
{
  // Transformare in secunde
  int timpIncalzire = timpIncalzireMinute * 60 + timpIncalzireSecunde;
  int timpMentinere = timpMentinereMinute * 60 + timpMentinereSecunde;
  int timpRacire = timpRacireMinute * 60 + timpRacireSecunde;

  float temperaturaSenzor = analogRead(A0); // citire temp senzor

  int TC = (temperaturaSenzor * 0.00488) * 100; // determinare temperatura in grade celsius

  lcd.clear();
  lcd.print("Temp dorita: ");
  lcd.print(temperatura);
  lcd.setCursor(0, 1);
  lcd.print("temp senz: ");
  lcd.print(TC);

  double b = millis();   // preluare nr milisec
  int nr_sec = b / 1000; // transformare in secunde

  if (nr_sec <= timpIncalzire)
  { // perioada crestere temperatura
    moving_setPoint = TC + (temperatura - TC) * nr_sec / timpIncalzire;
    schema = moving_setPoint;
  }
  else if (nr_sec <= (timpIncalzire + timpMentinere))
  { // perioada mentinere temperatura
    moving_setPoint = temperatura;
    schema = moving_setPoint;
  }
  else if (nr_sec <= (timpIncalzire + timpMentinere + timpRacire))
  { // perioada racire temperatura
    moving_setPoint = TC + (temperatura - TC) - (temperatura - TC) * nr_sec / (timpIncalzire + timpMentinere + timpRacire);
    schema = moving_setPoint;
  }
  else
  {
    systemState = SYSTEM_START;
    Serial.print("Oprit");
    Serial.println(nr_sec);
  }
}

void PID_OUTPUT_CALCULATE(void)
{
  afisare_temp();

  float temperaturaSenzor = analogRead(A0); // citeste temperatura senzor

  int T_Current = (temperaturaSenzor * 0.00488) * 100; // Transformare in grade Celsius

  int eroare = 0;
  static int eroare_anterioara = 0;
  int output;
  static int suma_erori = 0;

  eroare = schema - T_Current;

  suma_erori = suma_erori + eroare;

  float derivata = eroare - eroare_anterioara;

  output = kp * eroare + ki * suma_erori + kd * derivata;

  if (output > 255)
  {
    output = 255;
  }
  else if (output < 0)
  {
    output = 0;
  }

  eroare_anterioara = eroare;
  Serial.println(output);

  analogWrite(HEATER_PIN, output); // factorul de umplere pentru bec
}

void printTime(int minute, int secunde)
{
  lcd.setCursor(0, 1);
  if (minute < 10)
  {
    lcd.print("0");
  }
  lcd.print(minute);
  lcd.print(":");
  if (secunde == 0)
  {
    lcd.print("0");
  }
  lcd.print(secunde);
}

void menuMelodie()
{
  lcd.clear();
  lcd.setCursor(3, 0);
  lcd.print("Happy New");
  lcd.setCursor(5, 1);
  lcd.print("Year!");
}

void menuMain(void)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Alege meniul: ");
  lcd.setCursor(0, 1);
}

void print_menu(enum Menus menu)
{
  lcd.clear();
  switch (menu)
  {
  case MENU_MAIN:
    menuMain();
    lcd.print("Niciun meniu");
    break;
  case MENU_KP:
    lcd.print("KP = ");
    lcd.print(kp);
    break;
  case MENU_KD:
    lcd.print("KD = ");
    lcd.print(kd);
    break;
  case MENU_KI:
    lcd.print("KI = ");
    lcd.print(ki);
    break;
  case MENU_Tset:
    lcd.print("TEMPERATURA = ");
    lcd.print(temperatura);
    break;
  case MENU_tracire:
    lcd.print("MENU_tracire");
    printTime(timpRacireMinute, timpRacireSecunde);
    break;
  case MENU_tmentinere:
    lcd.print("MENU_tmentinere");
    printTime(timpMentinereMinute, timpMentinereSecunde);
    break;
  case MENU_tincalzire:
    lcd.print("MENU_tincalzire");
    printTime(timpIncalzireMinute, timpIncalzireSecunde);
    break;
  case MENU_Start:
    lcd.print("Start incalzire");
    lcd.setCursor(0 , 1);
    lcd.print("OK ?");
    break;
  default:
    lcd.print("Meniu secret:");
    lcd.println("esti fericit?");
    break;
  }
}

void enter_menu(void)
{
  current_menu = scroll_menu;
  print_menu(current_menu);
}

void go_home(void)
{
  scroll_menu = MENU_MAIN;
  current_menu = scroll_menu;
  print_menu(current_menu);
}

void go_next(void)
{
  menuMain();

  scroll_menu = (Menus)((int)scroll_menu + 1);            // choose next menu
  scroll_menu = (Menus)((int)scroll_menu % MENU_MAX_NUM); // loop the menus

  if (scroll_menu == MENU_MAX_NUM)
  {
    scroll_menu = (Menus)((int)scroll_menu + 1);
  }

  switch (scroll_menu)
  {
  case MENU_MAIN:
    scroll_menu = MENU_MAIN;
    go_next();
    break;

  case MENU_KP:
    lcd.print("Meniu KP");
    break;

  case MENU_KD:
    lcd.print("Meniu KD");
    break;

  case MENU_KI:
    lcd.print("Meniu KI");
    break;

  case MENU_Tset:
    lcd.print("Meniu Tset");
    break;

  case MENU_tracire:
    lcd.print("Meniu t racire");
    break;

  case MENU_tmentinere:
    lcd.print("Meniu t mentinere");
    break;

  case MENU_Start:
    lcd.print("Start incalzire");
    break;

  default:
    lcd.print("Niciun meniu");
    break;
  }
}

void go_prev(void)
{
  menuMain();

  scroll_menu = (Menus)((int)scroll_menu - 1);            // choose previous menu
  scroll_menu = (Menus)((int)scroll_menu % MENU_MAX_NUM); // loop around

  if (scroll_menu == MENU_MAX_NUM)
  {
    scroll_menu = (Menus)((int)scroll_menu - 1);
  }

  switch (scroll_menu)
  {
  case MENU_MAIN:
    scroll_menu = MENU_MAX_NUM;
    go_prev();
    break;

  case MENU_KP:
    lcd.print("Meniu KP");
    break;

  case MENU_KD:
    lcd.print("Meniu KD");
    break;

  case MENU_KI:
    lcd.print("Meniu KI");
    break;

  case MENU_Tset:
    lcd.print("Meniu Tset");
    break;

  case MENU_tracire:
    lcd.print("Meniu t racire");
    break;

  case MENU_tmentinere:
    lcd.print("Meniu t mentinere");
    break;

  case MENU_Start:
    lcd.print("Start incalzire");
    break;

  default:
    lcd.print("Niciun meniu");
    break;
  }
}

void inc_kp(void)
{
  kp = kp + 0.1;
  print_menu(MENU_KP);
}

void dec_kp(void)
{
  kp = kp - 0.1;
  print_menu(MENU_KP);
}

void inc_kd(void)
{
  kd = kd + 0.1;
  print_menu(MENU_KD);
}

void dec_kd(void)
{
  kd = kd - 0.1;
  print_menu(MENU_KD);
}

void inc_ki(void)
{
  ki = ki + 0.1;
  print_menu(MENU_KD);
}

void dec_ki(void)
{
  ki = ki - 0.1;
  print_menu(MENU_KD);
}

void inc_temperatura(void)
{
  temperatura++;
  print_menu(MENU_Tset);
}

void dec_temperatura(void)
{
  temperatura--;
  print_menu(MENU_Tset);
}

void inc_timpIncalzire(void)
{
  if (timpRacireSecunde == 50)
  {
    timpRacireMinute++;
    timpRacireSecunde = 0;
  }
  else
  {
    timpRacireSecunde = timpRacireSecunde + 10;
  }
  print_menu(MENU_tincalzire);
}

void dec_timpIncalzire(void)
{
  if (timpIncalzireSecunde == 0)
  {
    if (timpIncalzireMinute > 0)
      timpIncalzireMinute--;
    timpIncalzireSecunde = 50;
  }
  else
  {
    timpIncalzireSecunde = timpIncalzireSecunde - 10;
  }
  print_menu(MENU_tincalzire);
}

void inc_timpRacire(void)
{
  if (timpRacireSecunde == 50)
  {
    timpRacireMinute++;
    timpRacireSecunde = 0;
  }
  else
  {
    timpRacireSecunde = timpRacireSecunde + 10;
  }
  print_menu(MENU_tracire);
}

void dec_timpRacire(void)
{
  if (timpRacireSecunde == 0)
  {
    if (timpRacireMinute > 0)
      timpRacireMinute--;
    timpRacireSecunde = 50;
  }
  else
  {
    timpRacireSecunde = timpRacireSecunde - 10;
  }
  print_menu(MENU_tracire);
}

void inc_timpMentinere(void)
{
  if (timpMentinereSecunde == 50)
  {
    timpMentinereMinute++;
    timpMentinereSecunde = 0;
  }
  else
  {
    timpMentinereSecunde = timpMentinereSecunde + 10;
  }
  print_menu(MENU_tmentinere);
}

void dec_timpMentinere(void)
{
  if (timpMentinereSecunde == 0)
  {
    if (timpMentinereMinute > 0)
      timpMentinereMinute--;
    timpMentinereSecunde = 50;
  }
  else
  {
    timpMentinereSecunde = timpMentinereSecunde - 10;
  }
  print_menu(MENU_tmentinere);
}

void startPID(void)
{
  systemState = SYSTEM_START;
}

void state_machine(enum Menus menu, enum Buttons button)
{
  sm[menu][button]();
}

Buttons GetButtons(void)
{
  enum Buttons ret_val = EV_NONE;

  // Check if the current time is greater than the last button press time + debounce delay
  if (millis() - lastButtonPressMillis >= debounceDelay)
  {
    if (digitalRead(BUTTON_OK) == HIGH)
    {
      ret_val = EV_OK;
    }
    else if (digitalRead(BUTTON_CANCEL) == HIGH)
    {
      ret_val = EV_CANCEL;
    }
    else if (digitalRead(BUTTON_NEXT) == HIGH)
    {
      ret_val = EV_NEXT;
    }
    else if (digitalRead(BUTTON_PREV) == HIGH)
    {
      ret_val = EV_PREV;
    }

    // If a button is pressed, update the last button press time
    if (ret_val != EV_NONE)
    {
      lastButtonPressMillis = millis();
    }
  }

  return ret_val;
}

void setup()
{
  Serial.begin(9600);
  lcd.begin(16, 2);
  pinMode(melodyPin, OUTPUT);
  
  pinMode(BUTTON_OK, INPUT);
  pinMode(BUTTON_CANCEL, INPUT);
  pinMode(BUTTON_NEXT, INPUT);
  pinMode(BUTTON_PREV, INPUT);

  digitalWrite(BUTTON_OK, LOW);
  digitalWrite(BUTTON_CANCEL, LOW);
  digitalWrite(BUTTON_NEXT, LOW);
  digitalWrite(BUTTON_PREV, LOW);
  
  menuMelodie();
}

void loop()
{
  if(melodie)
  {
  // Iterate through the melody and play each note
  for (int i = 0; i < sizeof(wish_melody) / sizeof(wish_melody[0]); i++) {
    int noteDuration = 1750 / wish_tempo[i];
    tone(melodyPin, wish_melody[i], noteDuration);

    // Pause between notes
    delay(noteDuration * 1.30);

    // Stop the tone to avoid overlapping notes
    noTone(melodyPin);
  }
  melodie=false;
  }
  else
  {
	if(start == false)
    {
      print_menu(MENU_MAIN);
      start = true;
    }
    
  // Loop indefinitely
  
  volatile Buttons event = GetButtons();
  if (event != EV_NONE)
  {
    state_machine(current_menu, event);
  }

  if (systemState == SYSTEM_START)
  {
    PID_OUTPUT_CALCULATE();
  }

  // Serial.print("Next: ");
  // Serial.println(digitalRead(BUTTON_NEXT));
    
  }
  delay(200);
}