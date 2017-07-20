/* task_pid_nr */
#include <linux/sched.h>

#include "dpi_conntrack_ko.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Igor V. Nikolaev <monster@vedga.com>");
MODULE_DESCRIPTION("DPI connection tracking support module");
MODULE_ALIAS("dpi_conntrack");

/* Предварительное объявление локальных функций */
static int __init dpi_conntrack_startup(void);
static void dpi_conntrack_cleanup(void);

/* Определение точек входа при загрузке и выгрузке модуля */
module_init(dpi_conntrack_startup);
module_exit(dpi_conntrack_cleanup);

/**
 * Инициализация при загрузке модуля
 * 
 * @return 
 */
static int __init dpi_conntrack_startup(void) {
    int ret;
    
    if(0 != (ret = dpi_conntrack_netns_startup())) {
        return ret;
    }
    
#if 1
    if(1) {
        /* Получаем netns, активный в окружении, где выполняется insmod/modprobe */
        struct net *net = get_net_ns_by_pid(task_pid_nr(current));
        
        dpi_conntrack_register_file("test", net);
    }
        
#else
    if(1) {
        struct net *net;
        
        rcu_read_lock();
        
        for_each_net_rcu(net) {
            dpi_conntrack_register_file("test", net);
        }
        
        rcu_read_unlock();
    }
#endif    
    
    return 0;
}

/**
 * Освобождение ресурсов при выгрузке модуля
 */
static void dpi_conntrack_cleanup(void) {
    dpi_conntrack_netns_cleanup();
}
