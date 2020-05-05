#include <hardware.hpp>
#include <kernel.hpp>

#include <stm32f10x.h>


// TODO: Make this somehow private to kernel::hardware or nameless namespace.
// This is workaround to use C++ variables symbols in inline assembly.
volatile kernel::task_context current_task_context;
volatile kernel::task_context next_task_context;
volatile uint32_t skip_store = 1;

namespace kernel::hardware
{
    constexpr uint32_t core_clock_freq_hz{72'000'000};
    constexpr uint32_t target_systick_timestamp_us{1000};
    constexpr uint32_t systick_prescaler{core_clock_freq_hz/target_systick_timestamp_us};
    
    void init()
    {
        SysTick_Config(systick_prescaler - 1);
        
        ITM->TCR |= ITM_TCR_ITMENA_Msk;  // ITM enable
        ITM->TER = 1UL;                  // ITM Port #0 enable
    }
    
    void start()
    {
        // Setup interrupts.
        NVIC_SetPriority(SysTick_IRQn, 10); // SysTick should be higher than PendSV.
        NVIC_SetPriority(PendSV_IRQn, 12);
        
        NVIC_EnableIRQ(SysTick_IRQn);
        NVIC_EnableIRQ(PendSV_IRQn);
    }

    namespace sp
    {
        uint32_t get()
        {
            return __get_PSP();
        }

        void set(uint32_t a_new_sp)
        {
            __set_PSP(a_new_sp);
        }
    }
}

namespace kernel::hardware::task
{
    void Stack::init(uint32_t a_routine)
    {
        // TODO: Do something with magic numbers.
        m_data[TASK_STACK_SIZE - 8] = 0xCD'CD'CD'CD; // R0
        m_data[TASK_STACK_SIZE - 7] = 0xCD'CD'CD'CD; // R1
        m_data[TASK_STACK_SIZE - 6] = 0xCD'CD'CD'CD; // R2
        m_data[TASK_STACK_SIZE - 5] = 0xCD'CD'CD'CD; // R3
        m_data[TASK_STACK_SIZE - 4] = 0; // R12
        m_data[TASK_STACK_SIZE - 3] = 0; // LR R14
        m_data[TASK_STACK_SIZE - 2] = a_routine;
        m_data[TASK_STACK_SIZE - 1] = 0x01000000; // xPSR
    }
    
    uint32_t Stack::getStackPointer()
    {
        return (uint32_t)&m_data[TASK_STACK_SIZE - 8];
    }
}

extern "C"
{
    void SysTick_Handler(void)
    {
        bool execute_context_switch = kernel::tick(current_task_context, next_task_context);  // TODO: Make this function explicitly inline.
        
        if (execute_context_switch)
        {
            SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk; // Set PendSV_Handler to pending state.
        }
        
        __DMB(); // This is not needed in this case, due to write buffer being cleared on interrupt exit,
                 // but it is nice to have explicit information that memory write delay is taken into account.
    }
    
    __attribute__ (( naked )) void PendSV_Handler(void) // Use 'naked' attribute to remove C ABI, because return from interrupt must be set manually.
    {
        __ASM("CPSID I\n");
        
        // If skip_store is non-zero skip store_current_task. This is workaround for starting first task.
        // TODO: This workaround has to go.
        __ASM("ldr r2, =skip_store\n"); // TODO: this can be optimized to 1 instruction.
        __ASM("ldr r0, [r2]\n");
        __ASM("cbnz r0, load_next_task;\n");
        
        __ASM("store_current_task:\n");
        __ASM("ldr r0, =current_task_context\n");
        __ASM("ldr r1, [r0]\n");
        __ASM("stm r1, {r4-r11}\n");
        
        __ASM("load_next_task:\n");
        __ASM("ldr r0, =next_task_context\n");
        __ASM("ldr r1, [r0]\n");
        __ASM("ldm r1, {r4-r11}\n");
        
        // Set skip_store to 0.
        __ASM("mov r0, #0\n");
        __ASM("nop\n");
        __ASM("str r0, [r2]\n");
        
        // 0xFFFFFFFD in r0 means 'return to thread mode'.
        // Without this PendSV would return to SysTick
        // losing current thread state along the way.
        __ASM("ldr r0, =0xFFFFFFFD \n");
        __ASM("CPSIE I \n");
        __ASM("bx r0");
    }
}
