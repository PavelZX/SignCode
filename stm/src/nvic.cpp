#include "nvic.h"

#include "stm32f2xx.h"
#include "stm32f2xx_hal.h"
#include "stm32f2xx_hal_tim.h"

#include "srv.h"
#include "matrix.h"

extern led::Matrix<led::FrameBuffer<64, 32>> matrix;
extern srv::Servicer servicer;

void nvic::init() {
	NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);

	NVIC_SetPriority(DMA2_Stream5_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),6, 0));
	NVIC_EnableIRQ(DMA2_Stream5_IRQn);

	NVIC_SetPriority(TIM1_BRK_TIM9_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),5, 0));
	NVIC_EnableIRQ(TIM1_BRK_TIM9_IRQn);

	NVIC_SetPriority(DMA1_Stream5_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),4, 0));
	NVIC_EnableIRQ(DMA1_Stream5_IRQn);

	NVIC_SetPriority(DMA1_Stream6_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),6, 0));
	NVIC_EnableIRQ(DMA1_Stream6_IRQn);
}

extern "C" void DMA2_Stream5_IRQHandler() {
	if (LL_DMA_IsActiveFlag_TC5(DMA2)) {
		LL_DMA_ClearFlag_TC5(DMA2);
		matrix.dma_finish();
	}
}

extern "C" void TIM1_BRK_TIM9_IRQHandler() {
	if (LL_TIM_IsActiveFlag_UPDATE(TIM9)) {
		LL_TIM_ClearFlag_UPDATE(TIM9);
		matrix.tim_elapsed();
	}
}

extern "C" void DMA1_Stream5_IRQHandler() {
	if (LL_DMA_IsActiveFlag_TC5(DMA1)) {
		LL_DMA_ClearFlag_TC5(DMA1);
		servicer.dma_finish(true);
	}
}

extern "C" void DMA1_Stream6_IRQHandler() {
	if (LL_DMA_IsActiveFlag_TC6(DMA1)) {
		LL_DMA_ClearFlag_TC6(DMA1);
		servicer.dma_finish(false);
	}
}
