#include "dpi_conntrack_ko.h"

#include <net/netns/generic.h>

/* Каталог для создаваемой нами подсистемы внутри "/proc/net" */
#define PROC_NET_DPI    "dpi"

/* Предварительное описание локальных функций модуля */
static int __net_init dpi_conntrack_net_init(struct net *net);
static void __net_exit dpi_conntrack_net_exit(struct net *net);

/* Идентификатор реализуемой нами подсистемы в netns */
static int dpi_conntrack_net_id __read_mostly;

/* Набор операций и уведомлений, которыми мы будем пользоваться при работе с netns */
struct pernet_operations dpi_conntrack_net_ops = {
    .init = dpi_conntrack_net_init,
    .exit = dpi_conntrack_net_exit,
    /* Куда нам запишут присвоенный нашей подсистеме идентификатор */
    .id     = &dpi_conntrack_net_id,
    /* Размер private-данных нашей подсистемы */
    .size   = sizeof(struct dpi_conntrack_net)
};

/**
 * Регистрация создаваемой нами подсистемы в netns
 * 
 * @return 
 */
int __init dpi_conntrack_netns_startup(void) {
    return register_pernet_subsys(&dpi_conntrack_net_ops);
}

/**
 * Отмена регистрации создаваемой нами подсистемы в netns
 */
void dpi_conntrack_netns_cleanup(void) {
    unregister_pernet_subsys(&dpi_conntrack_net_ops);
}

/**
 * Получить private-область, которую зарезервировали для нашей подсистемы в netns
 * 
 * @param net
 * @return 
 */
struct dpi_conntrack_net *dpi_conntrack_pernet(struct net *net) {
    return net_generic(net, dpi_conntrack_net_id);
}

/**
 * Отработка уведомления о добавлении net ns (также вызывается при загрузке модуля)
 * 
 * @param net
 * @return 
 * 
 * NB!
 * Данный вызов выполняется в контексте ядра in_atomic() == 1!
 * (т.е. нельзя вызывать функции, требующие какого-либо ожидания).
 */
static int __net_init dpi_conntrack_net_init(struct net *net) {
    /* Область в netns для нашей подсистемы */
    struct dpi_conntrack_net *pernet = dpi_conntrack_pernet(net);
    
    /* Инициализация spinlock */
    spin_lock_init(&pernet->lock);
    
    /* Инициализируем hashtable с хранилищем зарегистрированных "файлов" */
    hash_init(pernet->files);
    
    /* Создаем каталог /proc/net/dpi для указанной netns */
    if(NULL == (pernet->proc_dpi = proc_mkdir(PROC_NET_DPI, net->proc_net))) {
        return -ENOMEM;
    }
    
    return 0;
}

/**
 * Отработка уведомления об удалении net ns (также вызывается при выгрузке модуля)
 * 
 * @param net
 */
static void __net_exit dpi_conntrack_net_exit(struct net *net) {
    /* Область в netns для нашей подсистемы */
    struct dpi_conntrack_net *pernet = dpi_conntrack_pernet(net);
    struct dpi_conntrack_file *f;
    struct hlist_node *tmp;
    int bkt;
    
    rcu_read_lock();
    
    /* Убеждаемся в том, что для удаляемой netns у нас нет зарегистрированных элементов */
    hash_for_each_safe(pernet->files, bkt, tmp, f, link) {
        /* Найден зарегистрированный элемент, снять его с регистрации */
        dpi_conntrack_procfs_unregister_file(f);
    }
    
    rcu_read_unlock();
    
    if(pernet->proc_dpi) {
        /* Удаляем каталог /proc/net/dpi */
        proc_remove(pernet->proc_dpi);
    }
}
