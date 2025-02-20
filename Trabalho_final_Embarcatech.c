#include <stdio.h>
#include "pico/stdlib.h"
#include <math.h>
#include <stdlib.h>  // Biblioteca padrão para funções básicas do Pico, como GPIO e temporização.
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/adc.h" // Biblioteca para controle do ADC (Conversor Analógico-Digital).


#define R25 10000.0f // Resistência do termistor a 25°C em Ohms
#define A 0.001129148 // Coeficiente A da equação de Steinhart-Hart
#define B 0.000234125 // Coeficiente B da equação de Steinhart-Hart
#define C 0.000000085 // Coeficiente C da equação de Steinhart-Hart
#define R1 10000.0f // Resistência em serie com o termistor
#define VREF 3.3f // Coeficiente C da equação de Steinhart-Hart


// #include "hardware/timer.h"
// #include <stdio.h>
// #include <stdlib.h> // Necessário para malloc e free
// // #include <stdio.h>             // Biblioteca padrão para entrada e saída, utilizada para printf.
// // // #include <stdlib.h> // Necessário para malloc e free
// // #include "pico/stdlib.h"       // Biblioteca padrão para funções básicas do Pico, como GPIO e temporização.
// #include "hardware/adc.h"


// Definições dos pinos para o joystick e botão
#define VRX_PIN 26    // Define o pino GP26 para o eixo X do joystick (Canal ADC0).
#define VRY_PIN 27    // Define o pino GP27 para o eixo Y do joystick (Canal ADC1).
#define SW_PIN 22     // Define o pino GP22 para o botão do joystick (entrada digital).
#define ADC_TEMPERATURE_CHANNEL 4   // Canal ADC que corresponde ao sensor de temperatura interno

// ATUADORES

const uint LED_Green = 11;            // Pino do LED conectado
const uint LED_Blue = 18; // era 12 LED interno            // Pino do LED conectado
const uint LED_Red = 13;    // Pino do LED conectado
// Estrutura para passar dados para o callback do alarme (continua igual)
struct alarm_data {
    uint led_pin;
    volatile bool *led_active_ptr; // Ponteiro para a variável led_active correspondente
    int led_number; // Número do LED (1, 2 ou 3) para mensagens
};

int64_t turn_off_callback(alarm_id_t id, void *user_data) {
    struct alarm_data *data = (struct alarm_data *)user_data;
    gpio_put(data->led_pin, false);
    *(data->led_active_ptr) = false; // Desliga o LED usando o ponteiro
    printf("LED %d desligado (pino %d)\n", data->led_number, data->led_pin);
    free(data);
    return 0;
}



float adc_to_temperature(uint16_t adc_value) {
    // Constantes fornecidas no datasheet do RP2040
    const float conversion_factor = 3.3f / (1 << 12);  // Conversão de 12 bits (0-4095) para 0-3.3V
    float voltage = adc_value * conversion_factor;     // Converte o valor ADC para tensão
    float temperature_local = 27.0f - (voltage - 0.706f) / 0.001721f;  // Equação fornecida para conversão
    return temperature_local;
}

void setup_var_ADC(){
    // Inicializa a comunicação serial para permitir o uso de printf.
    // Isso permite enviar mensagens para o console via USB, facilitando a depuração.
    stdio_init_all();

    // Inicializa o módulo ADC do Raspberry Pi Pico.
    // Isso prepara o ADC para ler valores dos pinos analógicos.
    adc_init();

    // Configura o pino GP26 para leitura analógica do ADC.
    // O pino GP26 está mapeado para o canal 0 do ADC, que será usado para ler o eixo X do joystick.
    adc_gpio_init(VRX_PIN);

    // Configura o pino GP27 para leitura analógica do ADC.
    // O pino GP27 está mapeado para o canal 1 do ADC, que será usado para ler o eixo Y do joystick.
    adc_gpio_init(VRY_PIN);

    // Configura o pino do sensor de umidade
    adc_gpio_init(28); // GPIO28 é o ADC2

    
    // Configura o pino do botão como entrada digital com pull-up interno.
    // O pull-up garante que o pino leia "alto" quando o botão não está pressionado, evitando leituras instáveis.
    gpio_init(SW_PIN);
    gpio_set_dir(SW_PIN, GPIO_IN);
    gpio_pull_up(SW_PIN);


    // Seleciona o canal 4 do ADC (sensor de temperatura interno)
    adc_set_temp_sensor_enabled(true);  // Habilita o sensor de temperatura interno

}

void setup_var_GPIO(){

gpio_init(LED_Red);
gpio_set_dir(LED_Red, GPIO_OUT);
gpio_put(LED_Red, 0); // LED começa apagado

gpio_init(LED_Green);
gpio_set_dir(LED_Green, GPIO_OUT);
gpio_put(LED_Green, 0); // LED começa apagado

gpio_init(LED_Blue);
gpio_set_dir(LED_Blue, GPIO_OUT);
gpio_put(LED_Blue, 0); // LED começa apagado

}
volatile bool led_on_red = false, led_on_green = false, led_on_blue = false; // Estados individuais dos LEDs
volatile bool led_active_red = false, led_active_green = false, led_active_blue = false; // Atividade individual dos LEDs

bool repeating_timer_callback(struct repeating_timer *t) {
    static absolute_time_t last_activated_time = 0; // Contar para tempo
    static absolute_time_t last_activated_time_red = 0; // Contar para tempo medida de umidade ou temperatura externa
    static absolute_time_t last_activated_time_blue = 0; // Contar para tempo medida de umidade ou temperatura externa
    static int flag = 1; // FLAG PARA LER 0 -> Temperatura / 1 -> UMIDADE

    uint led_pin_to_use_green = 0;
    volatile bool *led_active_to_use = NULL;

    uint led_pin_to_use_red = 0;
    volatile bool *led_active_to_use_red = NULL;
    
    
    uint led_pin_to_use_blue = 0;
    volatile bool *led_active_to_use_blue = NULL;

    int led_number = 0;
    // Seleciona o canal 0 do ADC (pino GP26) para leitura.
    // Esse canal corresponde ao eixo X do joystick (VRX).
    adc_select_input(0);
    uint16_t vrx_value = adc_read(); // Lê o valor do eixo X, de 0 a 4095.

    // Seleciona o canal 1 do ADC (pino GP27) para leitura.
    // Esse canal corresponde ao eixo Y do joystick (VRY).
    adc_select_input(1);
    uint16_t vry_value = adc_read(); // Lê o valor do eixo Y, de 0 a 4095.

    // Lê o estado do botão do joystick (SW).
    // O valor lido será 0 se o botão estiver pressionado e 1 se não estiver.
    bool sw_value = !gpio_get(SW_PIN); // 0 indica que o botão está pressionado.

    // Imprime os valores lidos na comunicação serial.
    // VRX e VRY mostram a posição do joystick, enquanto SW mostra o estado do botão.
    printf("VRX: %u, VRY: %u, SW: %d\n", vrx_value, vry_value, sw_value);


    // Lê o valor do ADC no canal selecionado (sensor de temperatura)
    adc_select_input(ADC_TEMPERATURE_CHANNEL);  // Seleciona o canal do sensor de temperatura
    uint16_t adc_value = adc_read();

    // Converte o valor do ADC para temperatura em graus Celsius
    float temperature = adc_to_temperature(adc_value);

    // Imprime a temperatura na comunicação serial
    printf("Temperatura sensor interno: %.2f °C\n", temperature);


    if (vrx_value > 2048) {
            led_pin_to_use_green = LED_Green;
            led_active_to_use = &led_active_green;
            led_number = 2;
        }



    if ((led_pin_to_use_green != 0) && (absolute_time_diff_us(last_activated_time, get_absolute_time()) > 1000)) { // Verifica se um LED foi selecionado
      
      last_activated_time = get_absolute_time();
      gpio_put(led_pin_to_use_green, true);
      *led_active_to_use = true;
      printf("LED %d ligado (pino %d)\n", led_number, led_pin_to_use_green);

      struct alarm_data *alarm_data = malloc(sizeof(struct alarm_data));
      if (alarm_data == NULL) {
          printf("Erro ao alocar memória para o alarme!\n");
          return true;
      }
      alarm_data->led_pin = led_pin_to_use_green;
      alarm_data->led_active_ptr = led_active_to_use; // Passa o ponteiro
      alarm_data->led_number = led_number;

      add_alarm_in_ms(4000, turn_off_callback, alarm_data, false);

  }




  if (vry_value > 2048) {
    led_pin_to_use_blue = LED_Blue;
    led_active_to_use = &led_active_blue;
    led_number = 3;
}



if ((led_pin_to_use_blue == LED_Blue) && (absolute_time_diff_us(last_activated_time_blue, get_absolute_time()) > 1000)) { // Verifica se um LED foi selecionado

last_activated_time_blue = get_absolute_time();
gpio_put(led_pin_to_use_blue, true);
*led_active_to_use = true;
printf("LED %d ligado (pino %d)\n", led_number, led_pin_to_use_green);

struct alarm_data *alarm_data_blue = malloc(sizeof(struct alarm_data));
if (alarm_data_blue == NULL) {
  printf("Erro ao alocar memória para o alarme!\n");
  return true;
}
alarm_data_blue->led_pin = led_pin_to_use_blue;
alarm_data_blue->led_active_ptr = led_active_to_use_blue; // Passa o ponteiro
alarm_data_blue->led_number = led_number;

add_alarm_in_ms(4000, turn_off_callback, alarm_data_blue, false);

}













// Seleciona o canal de entrada ADC
adc_select_input(2); // ADC2
const float conversion_factor = 3.3f / (1 << 12);
uint16_t result = adc_read();
float rthermistor = R1 * result * conversion_factor / (VREF - result * conversion_factor);
float temperature_sensor = 1.0f / (A + B * logf(rthermistor) + C * powf(logf(rthermistor), 3.0f)) - 273.15f;
float umidade = ((result * conversion_factor - 3.3)/(-3.3+1.666)) *100;
if(umidade < 0.0){
    umidade=0.0;
}
if(umidade > 100.0){
    umidade=100.0;
} 
if (flag ==0){
    printf("Umidade é %.3f %%\n",umidade);
    if (umidade < 85) {
        led_pin_to_use_red = LED_Red;
        led_active_to_use = &led_active_red;
        led_number = 1;
}
    
    
}




if (flag ==1){
printf("Temperatura do sensor externo é %.3f Celsius\n",temperature_sensor);
if (temperature_sensor >= 40.0) {
    led_pin_to_use_red = LED_Red;
    led_active_to_use = &led_active_red;
    led_number = 1;
}


}



printf("Raw value: 0x%03x, voltage: %.3f V\n", result, result * conversion_factor);





if ((led_pin_to_use_red == LED_Red) && (absolute_time_diff_us(last_activated_time_red, get_absolute_time()) > 5000*1000)) { // Verifica se um LED foi selecionado

last_activated_time_red = get_absolute_time();
gpio_put(led_pin_to_use_red, true);
*led_active_to_use = true;
printf("LED %d ligado (pino %d)\n", led_number, led_pin_to_use_green);

struct alarm_data *alarm_data_red = malloc(sizeof(struct alarm_data));
if (alarm_data_red == NULL) {
  printf("Erro ao alocar memória para o alarme!\n");
  return true;
}
alarm_data_red->led_pin = led_pin_to_use_red;
alarm_data_red->led_active_ptr = led_active_to_use_red; // Passa o ponteiro
alarm_data_red->led_number = led_number;

add_alarm_in_ms(4000, turn_off_callback, alarm_data_red, false);

}






led_pin_to_use_red=0;
return true; // Continuar o temporizador de repetição
}

int main() {
    setup_var_ADC();

    setup_var_GPIO();








    struct repeating_timer timer;
    add_repeating_timer_ms(1000, repeating_timer_callback, NULL, &timer);
    // Loop infinito para ler continuamente os valores do joystick e do botão.
    while (true) {


        // // Introduz um atraso de 500 milissegundos antes de repetir a leitura.
        // // Isso evita que as leituras e impressões sejam feitas muito rapidamente.
        sleep_ms(500);
    }

    // Retorna 0 indicando que o programa terminou com sucesso.
    // Esse ponto nunca será alcançado, pois o loop é infinito.
    return 0;
}


// int main()
// {
//     stdio_init_all();

//     while (true) {
//         printf("Hello, world!\n");
//         sleep_ms(1000);
//     }
// }
