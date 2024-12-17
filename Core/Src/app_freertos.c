/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "adc.h"
#include "dma.h"
#include "iwdg.h"
#include "usart.h"
#include "gpio.h"

#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <uxr/client/transport.h>
#include <rmw_microxrcedds_c/config.h>
#include <rmw_microros/rmw_microros.h>

#include <micro_ros_utilities/string_utilities.h>
#include <std_msgs/msg/float32.h>
#include <std_msgs/msg/float32_multi_array.h>
#include <std_msgs/msg/float64_multi_array.h>

#include <std_msgs/msg/u_int16.h>
#include <std_msgs/msg/bool.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define BUFFER_SIZE 40
#define RCSOFTCHECK(fn) if (fn != RCL_RET_OK) {};
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
rcl_node_t node;

rcl_publisher_t xrl8_publisher;
std_msgs__msg__UInt16 XRL8_msg;

rcl_publisher_t emer_publisher;
std_msgs__msg__Bool emer_msg;

uint16_t sync_counter = 0;
int l = 1000;

uint16_t pinState = 0;
uint16_t adc_buffer[BUFFER_SIZE];
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
uint32_t defaultTaskBuffer[ 5000 ];
osStaticThreadDef_t defaultTaskControlBlock;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_mem = &defaultTaskBuffer[0],
  .stack_size = sizeof(defaultTaskBuffer),
  .cb_mem = &defaultTaskControlBlock,
  .cb_size = sizeof(defaultTaskControlBlock),
  .priority = (osPriority_t) osPriorityHigh,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
bool cubemx_transport_open(struct uxrCustomTransport * transport);
bool cubemx_transport_close(struct uxrCustomTransport * transport);
size_t cubemx_transport_write(struct uxrCustomTransport* transport, const uint8_t * buf, size_t len, uint8_t * err);
size_t cubemx_transport_read(struct uxrCustomTransport* transport, uint8_t* buf, size_t len, int timeout, uint8_t* err);

void * microros_allocate(size_t size, void * state);
void microros_deallocate(void * pointer, void * state);
void * microros_reallocate(void * pointer, size_t size, void * state);
void * microros_zero_allocate(size_t number_of_elements, size_t size_of_element, void * state);


void timer_callback(rcl_timer_t * timer, int64_t last_call_time);
uint16_t calculate_average(uint16_t *buffer, uint16_t length);
void xlr8_publish(uint16_t xlr8);
void emergency_publish(GPIO_PinState emer_state);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
	HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buffer, BUFFER_SIZE);
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */

	// micro-ROS configuration
	rmw_uros_set_custom_transport(
	true,
	(void *) &hlpuart1,
	cubemx_transport_open,
	cubemx_transport_close,
	cubemx_transport_write,
	cubemx_transport_read);

	rcl_allocator_t freeRTOS_allocator = rcutils_get_zero_initialized_allocator();
	freeRTOS_allocator.allocate = microros_allocate;
	freeRTOS_allocator.deallocate = microros_deallocate;
	freeRTOS_allocator.reallocate = microros_reallocate;
	freeRTOS_allocator.zero_allocate =  microros_zero_allocate;

	if (!rcutils_set_default_allocator(&freeRTOS_allocator)) {
	  printf("Error on default allocators (line %d)\n", __LINE__);
	}

	// micro-ROS app
	rcl_timer_t XLR8_timer;
	rclc_support_t support;
	rclc_executor_t executor;
	rcl_allocator_t allocator;
	rcl_init_options_t init_options;

	const unsigned int timer_period = RCL_MS_TO_NS(8);
	const int timeout_ms = 1000;
	int executor_num = 1;

	const rosidl_message_type_support_t * uint16_type_support =
	  ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, UInt16);

	const rosidl_message_type_support_t * bool_type_support =
	  ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Bool);


	allocator = rcl_get_default_allocator();

	executor = rclc_executor_get_zero_initialized_executor();

	init_options = rcl_get_zero_initialized_init_options();

	RCSOFTCHECK(rcl_init_options_init(&init_options, allocator));
	RCSOFTCHECK(rcl_init_options_set_domain_id(&init_options, 198));

	// create support init_options
	rclc_support_init_with_options(&support, 0, NULL, &init_options, &allocator);

	// create timer
	rclc_timer_init_default(&XLR8_timer, &support, timer_period, timer_callback);

	// create node
	rclc_node_init_default(&node, "uros_H7_Node", "", &support);

	// create publisher
	rclc_publisher_init_best_effort(&xrl8_publisher, &node, uint16_type_support, "accl_publisher");
	rclc_publisher_init_best_effort(&emer_publisher, &node, bool_type_support, "H7_Emergency");
	// create subscriber

	// create service server

	// create service client

	// create executor
	rclc_executor_init(&executor, &support.context, executor_num, &allocator);

	rclc_executor_add_timer(&executor, &XLR8_timer);

	rclc_executor_spin(&executor);
	rmw_uros_sync_session(timeout_ms);

//	for(;;)
//	{
//	//	osDelay(10);
//	}
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void timer_callback(rcl_timer_t * timer, int64_t last_call_time)
{
	if (timer != NULL) {


		if (uwTick >= l) {  // Sync session at lower frequency
			rmw_uros_sync_session(1000);
			l += 1000;
		}

		xlr8_publish(calculate_average(adc_buffer, BUFFER_SIZE));
		emergency_publish(HAL_GPIO_ReadPin(Emergency_GPIO_Port, Emergency_Pin));

		pinState = HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_2);
		if (pinState == 1){
				HAL_GPIO_WritePin(GPIOE, GPIO_PIN_6, GPIO_PIN_SET);
				HAL_GPIO_WritePin(GPIOE, GPIO_PIN_10, GPIO_PIN_SET);
				HAL_GPIO_WritePin(GPIOE, GPIO_PIN_12, GPIO_PIN_SET);
				HAL_GPIO_WritePin(GPIOE, GPIO_PIN_15, GPIO_PIN_SET);
				HAL_GPIO_WritePin(Lamp_Mode1_GPIO_Port, Lamp_Mode1_Pin, GPIO_PIN_SET);
				HAL_GPIO_WritePin(Lamp_Mode2_GPIO_Port, Lamp_Mode2_Pin, GPIO_PIN_SET);
				HAL_GPIO_WritePin(Lamp_Mode3_GPIO_Port, Lamp_Mode3_Pin, GPIO_PIN_SET);
				HAL_GPIO_WritePin(Lamp_Mode4_GPIO_Port, Lamp_Mode4_Pin, GPIO_PIN_SET);
		}
		else{
			HAL_GPIO_WritePin(GPIOE, GPIO_PIN_6, GPIO_PIN_RESET);
			HAL_GPIO_WritePin(GPIOE, GPIO_PIN_10, GPIO_PIN_RESET);
			HAL_GPIO_WritePin(GPIOE, GPIO_PIN_12, GPIO_PIN_RESET);
			HAL_GPIO_WritePin(GPIOE, GPIO_PIN_15, GPIO_PIN_RESET);
			HAL_GPIO_WritePin(Lamp_Mode1_GPIO_Port, Lamp_Mode1_Pin, GPIO_PIN_RESET);
		}
		HAL_IWDG_Refresh(&hiwdg);
	}

}

void xlr8_publish(uint16_t xlr8)
{
	XRL8_msg.data = xlr8;
	rcl_ret_t ret = rcl_publish(&xrl8_publisher, &XRL8_msg, NULL);
	if (ret != RCL_RET_OK) printf("Error publishing (line %d)\n", __LINE__);
}

void emergency_publish(GPIO_PinState emer_state)
{
	emer_msg.data = !emer_state;
	rcl_ret_t ret = rcl_publish(&emer_publisher, &emer_msg, NULL);
	if (ret != RCL_RET_OK) printf("Error publishing (line %d)\n", __LINE__);
}

uint16_t calculate_average(uint16_t *buffer, uint16_t length) {
    uint32_t sum = 0;
    for (uint16_t i = 0; i < length; i++) {
        sum += buffer[i];
    }
    return (uint16_t)(sum / length);
}
/* USER CODE END Application */

