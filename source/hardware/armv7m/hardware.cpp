#include <hardware.hpp>

#include <stm32f10x.h>

// TODO: Make this somehow private to kernel::hardware or nameless namespace.
// This is workaround to use C++ variables symbols in inline assembly.
volatile kernel::hardware::task::Context * current_task_context;
volatile kernel::hardware::task::Context * next_task_context;

// This containt hardware specific implementation.
// In current case its stm32f103ze clocked at 72MHz.
namespace kernel::hardware
{
    // Variables used to calculate SysTick prescaler used to reach 1 ms timestamp.
    // This is derived from formula:
    // TARGET_SYSTICK_TIMESTAMP_HZ = CORE_CLOCK_FREQ_HZ / SYSTICK_PRESCALER,
    // where SYSTICK_PRESCALER is searched value.
    constexpr uint32_t TARGET_SYSTICK_TIMESTAMP_HZ{ 1000U};
    constexpr uint32_t SYSTICK_PRESCALER{ CORE_CLOCK_FREQ_HZ / TARGET_SYSTICK_TIMESTAMP_HZ};
    
    namespace debug
    {
        void init()
        {
            ITM->TCR |= ITM_TCR_ITMENA_Msk;  // ITM enable
            ITM->TER = 1UL;                  // ITM Port #0 enable
        }
        void putChar( char c)
        {
            ITM_SendChar( c);
        }

        void print( const char * s)
        {
            while ( '\0' != *s)
            {
                kernel::hardware::debug::putChar( *s);
                ++s;
            }
        }

        void setBreakpoint()
        {
            __ASM(" BKPT 0\n");
        }
    }

    void syscall( SyscallId a_id)
    {
        __ASM(" DMB"); // Complete all explicit memory transfers
        
        switch( a_id)
        {
        case SyscallId::LoadNextTask:
            __ASM(" SVC #0");
            break;
        case SyscallId::ExecuteContextSwitch:
            __ASM(" SVC #1");
            break;
        }
    }

    void init()
    {
        SysTick_Config( SYSTICK_PRESCALER - 1U);

        debug::init();

        // Setup interrupts.
        // Set priorities - lower number is higher priority

        // Note: SysTick and PendSV interrupts use the same priority,
        //       to remove the need of critical section for use of shared
        //       kernel data. It is actually recommended in ARM reference manual.
        NVIC_SetPriority( SVCall_IRQn, 0U);
        NVIC_SetPriority( PendSV_IRQn, 1U);
        NVIC_SetPriority( SysTick_IRQn, 1U);
    }
    
    void start()
    {
        // Enable interrupts

        // Note: Enable SysTick last, since it is the only PendSV trigger.
        NVIC_EnableIRQ( SVCall_IRQn);
        NVIC_EnableIRQ( PendSV_IRQn);
        NVIC_EnableIRQ( SysTick_IRQn);
    }

    void waitForInterrupt()
    {
        __WFI();
    }

    namespace sp
    {
        uint32_t get()
        {
            // TODO: Thread mode should be used in final version
            return __get_PSP();
        }

        void set(uint32_t a_new_sp)
        {
            // TODO: Thread mode should be used in final version
            __set_PSP( a_new_sp);
        }
    }

    namespace context::current
    {
        void set( volatile kernel::hardware::task::Context * a_context)
        {
            current_task_context = a_context;
        }
    }

    namespace context::next
    {
        void set( volatile kernel::hardware::task::Context * a_context)
        {
            next_task_context = a_context;
        }
    }

    namespace critical_section
    {
        // TODO: Current implementation is naive.
        //       Disabling interrupts is never good solution.
        //       Implement priority 3 groups: kernel, user_high, user_low
        void lock( volatile Context & a_context)
        {
            // Store local context.
            a_context.m_local_data = __get_PRIMASK();
            // Mask all maskable interrupts.
            __disable_irq(); // __set_PRIMASK( 0U);
        }

        void unlock( volatile Context & a_context)
        {
            // Re-store primask to local context.
            __set_PRIMASK( a_context.m_local_data);
        }
    }
}

namespace kernel::hardware::task
{
    void Stack::init( uint32_t a_routine) volatile
    {
        // TODO: Do something with magic numbers.
        m_data[ TASK_STACK_SIZE - 8U] = 0xCD'CD'CD'CDU; // R0
        m_data[ TASK_STACK_SIZE - 7U] = 0xCD'CD'CD'CDU; // R1
        m_data[ TASK_STACK_SIZE - 6U] = 0xCD'CD'CD'CDU; // R2
        m_data[ TASK_STACK_SIZE - 5U] = 0xCD'CD'CD'CDU; // R3
        m_data[ TASK_STACK_SIZE - 4U] = 0U; // R12
        m_data[ TASK_STACK_SIZE - 3U] = 0U; // LR R14
        m_data[ TASK_STACK_SIZE - 2U] = a_routine;
        m_data[ TASK_STACK_SIZE - 1U] = 0x01000000U; // xPSR
    }
    
    uint32_t Stack::getStackPointer() volatile
    {
        return reinterpret_cast< uint32_t>( &m_data[ TASK_STACK_SIZE - 8]);
    }
}

extern "C"
{
    void __aeabi_assert(
        const char * expr,
        const char * file,
        int line
    )
    {
        kernel::hardware::debug::setBreakpoint();
    }
    
    inline __attribute__ (( naked )) void LoadTask(void)
    {
        __ASM(" CPSID I\n");

        // Load task context
        __ASM(" ldr r0, =next_task_context\n");
        __ASM(" ldr r1, [r0]\n");
        __ASM(" ldm r1, {r4-r11}\n");

        // 0xFFFFFFFD in r0 means 'return to thread mode' (use PSP).
        // TODO: first task should be initialized to Thread Mode
        __ASM(" ldr r0, =0xFFFFFFFD \n");
        __ASM(" CPSIE I \n");
        
        // This is not needed in this case, due to write buffer being cleared on interrupt exit,
        // but it is nice to have explicit information that memory write delay is taken into account.
        __ASM(" DSB"); // Complete all explicit memory transfers
        __ASM(" ISB"); // flush instruction pipeline
        
        __ASM(" bx r0");
    }

    void SVC_Handler_Main( unsigned int * svc_args)
    {
        // This handler implementation is taken from official reference manual.
        // svc_args points to context stored when SVC interrupt was activated.
 
        // Stack contains:
        // r0, r1, r2, r3, r12, r14, the return address and xPSR
        // First argument (r0) is svc_args[0]
        unsigned int  svc_number = ( ( char * )svc_args[ 6U ] )[ -2U ] ; // TODO: simplify this bullshit obfuscation
        switch( svc_number )
        {
            case 0U:       // SyscallId::LoadNextTask:
            {
                kernel::internal::loadNextTask();
                __DSB(); // Complete unfinished memory transfers from loadNextTask.
                LoadTask();
                break;
            }
            case 1U:       // SyscallId::ExecuteContextSwitch
            {
                kernel::internal::switchContext();
                __DSB(); // Complete unfinished memory transfers from switchContext.
                __ISB(); // Flush instructions in pipeline before entering PendSV.
                SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk; // Set PendSV_Handler to pending state so it can tail chain from SVC.
                break;
            }
            default:      // unknown SVC
                break;
        }
    }

    __attribute__ (( naked )) void SVC_Handler(void)
    {
        // This handler implementation is taken from official reference manual. Since SVC assembly instruction store argument in opcode itself,
        // code bellow track instruction address that invoked SVC interrupt via context stored when interrupt was activated.
        __ASM(" TST lr, #4\n");  // lr AND #4 - Test if masked bits are set. 
        __ASM(" ITE EQ\n"        // Next 2 instructions are conditional. EQ - equal - Z(zero) flag == 1. Check if result is 0.
        " MRSEQ r0, MSP\n"       // Move the contents of a special register to a general-purpose register. It block with EQ.
        " MRSNE r0, PSP\n");     // It block with NE -> z == 0 Not equal
        __ASM(" B SVC_Handler_Main\n");
    }

    void SysTick_Handler(void)
    {
        // TODO: Make this function explicitly inline.
        bool execute_context_switch = kernel::internal::tick();

        __DSB(); // Complete unfinished memory transfers from tick function.

        if ( execute_context_switch)
        {
            SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk; // Set PendSV interrupt to pending state.
        }
    }
    
    __attribute__ (( naked )) void PendSV_Handler(void) // Use 'naked' attribute to remove C ABI, because return from interrupt must be set manually.
    {
        __ASM(" CPSID I\n");
        
        // Store current task at address provided by current_task_context.
        __ASM(" ldr r0, =current_task_context\n");
        __ASM(" ldr r1, [r0]\n");
        __ASM(" stm r1, {r4-r11}\n");
        
        // Load task context from address provided by next_task_context.
        __ASM(" ldr r0, =next_task_context\n");
        __ASM(" ldr r1, [r0]\n");
        __ASM(" ldm r1, {r4-r11}\n");
        
        // 0xFFFFFFFD in r0 means 'return to thread mode' (use PSP).
        // Without this PendSV would return to SysTick
        // losing current thread state along the way.
        __ASM(" ldr r0, =0xFFFFFFFD \n");
        __ASM(" CPSIE I \n");
        
        // This is not needed in this case, due to write buffer being cleared on interrupt exit,
        // but it is nice to have explicit information that memory write delay is taken into account.
        __ASM(" DSB"); // Complete all explicit memory transfers
        __ASM(" ISB"); // Flush instruction pipeline before branching to next task.
        
        __ASM(" bx r0");
    }
}
