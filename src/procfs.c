#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/rcupdate.h>

#include <linux/rculist_nulls.h>
#include <net/netfilter/nf_conntrack_helper.h>


#include "dpi_conntrack_ko.h"

/* Данные итератора для последовательного чтения файла */
struct ct2_iter_state {
    /* Элемент должен присутствовать и обязательно быть первым! */
    struct seq_net_private p;
    /* Глобальный указатель на дескриптор */
    struct dpi_conntrack_file *fg;
    unsigned int bucket;
    
};

struct dpi_iterator {
    unsigned int bucket;
    struct hlist_nulls_node *head;
};


/* Предварительное объявление локальных функций модуля */
static int dpi_file_open(struct inode *inode, struct file *file);
static void *dpi_seq_start(struct seq_file *s, loff_t *pos) __acquires(RCU);
static void *dpi_seq_next(struct seq_file *s, void *v, loff_t *pos);
static void dpi_seq_stop(struct seq_file *s, void *v)  __releases(RCU);
static int dpi_seq_show(struct seq_file *s, void *v);
static void dpi_conntrack_file_cleanup_rcu(struct rcu_head *head);

static struct dpi_iterator *ct_get_idx(struct dpi_iterator *i, struct dpi_conntrack_file *f, loff_t pos);
static void ct_get_next(struct dpi_iterator *i, struct net *net);
static void ct_get_first(struct dpi_iterator *i, struct net *net);
static int is_this_helper(struct nf_conntrack_tuple_hash *hash, const char *name);

/* Набор операций для файла */
static const struct file_operations file_ops = {
    .owner   = THIS_MODULE,
    .open    = dpi_file_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = seq_release,
};

/* Набор операций для последовательного чтения файла */
static const struct seq_operations seq_ops = {
    .start = dpi_seq_start,
    .next  = dpi_seq_next,
    .stop  = dpi_seq_stop,
    .show  = dpi_seq_show
};



int dpi_conntrack_register_file(const char *name, struct net *net) {
    struct dpi_conntrack_file *fg;
    int rv;
    /* Область в netns для нашей подсистемы */
    struct dpi_conntrack_net *pernet = dpi_conntrack_pernet(net);
    
    rcu_read_lock();
    
    /* Убеждаемся в том, что данный файл пока отсутствует, выделяем память
     * под его дескриптор и добавляем его в hashtable files в структуре pernet.
     */
    rv = dpi_conntrack_file_new_rcu(net, pernet, name, &fg);
    
    rcu_read_unlock();
    
    if(!rv) {
        /* Ошибок нет, можно создавать файл с запрошенным именем */
        struct proc_dir_entry *pde = proc_create_data(name, 0440, pernet->proc_dpi, &file_ops, fg);
        
        pr_info("dpi_conntrack_register_file: Create new procfs net file %s\n", name);
        
        if(pde) {
            rcu_read_lock();
            
            rcu_dereference(fg)->pde = pde;
            
            rcu_read_unlock();
            
            pr_info("dpi_conntrack_register_file: Create new procfs net file %s complete.\n", name);
        } else {
            /* Не удалось создать файл в procfs */
            rcu_read_lock();
            
            dpi_conntrack_procfs_unregister_file(fg);
            
            rcu_read_unlock();
            
            rv = -ENOMEM;
        }
    }

    return rv;
}
EXPORT_SYMBOL_GPL(dpi_conntrack_register_file);

int dpi_conntrack_unregister_file(const char *name, struct net *net) {
    struct dpi_conntrack_file *f;
    /* Область в netns для нашей подсистемы */
    struct dpi_conntrack_net *pernet = dpi_conntrack_pernet(net);
    
    rcu_read_lock();
    
    /* Найти существующий элемент */
    f = dpi_conntrack_file_find_rcu(pernet, name);
    
    if(f) {
        /* Запрос освобождения ресурсов */
        dpi_conntrack_procfs_unregister_file(f);
    }
    
    rcu_read_unlock();
    
    /* Возвращаем результат операции */
    return f ? 0 : -ENOENT;
}
EXPORT_SYMBOL_GPL(dpi_conntrack_unregister_file);

/**
 * Запрос освобождения ресурсов
 * 
 * @param f
 */
void dpi_conntrack_procfs_unregister_file(struct dpi_conntrack_file *f) {
    /* Удаляем элемент из таблицы (д.б. в окружении rcu_read_lock!) */
    hash_del_rcu(&f->link);

    /* Ресурсы можно освобождать после истечения grace period */
    call_rcu(&f->rcu, dpi_conntrack_file_cleanup_rcu);
}

/**
 * Операция открытия файла для последовательного чтения
 * 
 * @param inode
 * @param file
 * @return 
 */
static int dpi_file_open(struct inode *inode, struct file *file) {
    int rv = seq_open(file, &seq_ops);
    
    if(!rv) {
        /* Файл успешно открыт */
        struct seq_file *s = file->private_data;
        
        /* struct dpi_conntrack_file *fg переносим в seq_file->private */
        s->private = PDE_DATA(inode);
    }
    
    return rv;
}

/**
 * 
 * @param s
 * @param pos
 * @return 
 * 
 * Возвращает итератор начиная с позиции *pos
 */
static void *dpi_seq_start(struct seq_file *s, loff_t *pos) __acquires(RCU) {
    /* Создаем итератор */
    struct dpi_iterator *i = kzalloc(sizeof(struct dpi_iterator), GFP_ATOMIC);
    
    if(i) {
        /* Итератор создан */
        struct dpi_iterator *first;
        
        rcu_read_lock();
        
        /* Определяем первую подходящую позицию */
        first = ct_get_idx(i, rcu_dereference(s->private), *pos);
        
        rcu_read_unlock();

        pr_info("dpi_seq_start(...) with first=0x%llx\n", (u64)first);
        
        if(first) {
            /* Итератор на первую позицию */
            return first;
        }
        
        /* Нет ни одной записи, освобождаем память */
        kfree(i);
    }
    
    /* Нет действующего итератора */
    return NULL;
}

/**
 * Перемещение итератора на следующую позицию
 * 
 * @param s
 * @param v
 * @param pos
 * @return 
 */
static void *dpi_seq_next(struct seq_file *s, void *v, loff_t *pos) {
    if(v) {
        struct dpi_conntrack_file *f;
        struct dpi_iterator *i = v;
        
        (*pos)++;

        rcu_read_lock();

        f = rcu_dereference(s->private);

        pr_info("dpi_seq_next(...) for %s\n", f->name);
        
        do {
            ct_get_next(i, f->net);
        } while(i->head && !is_this_helper((struct nf_conntrack_tuple_hash *)i->head, f->name));
        
        rcu_read_unlock();
        
        /* Если еще есть элементы, то возвращаем итератор */
        return i->head ? i : NULL;
    }
    
    return NULL;

#if 0    
#if 1
    (*pos)++;

    if(v) {
        struct dpi_conntrack_file *f;
        
        rcu_read_lock();
        
        /* Наша управляющая структура */
        f = rcu_dereference(s->private);
        
        pr_info("dpi_seq_next(...) for %s\n", f->name);
        
        rcu_read_unlock();
    }
    
    return NULL;
#else
    struct ct2_iter_state *st = s->private;
    struct dpi_conntrack_file *f = rcu_dereference(st->fg);
    
    (*pos)++;
    
    do {
        v = ct_get_next(s, v);
    } while(v && !is_this_helper((struct nf_conntrack_tuple_hash *)v, f->name));
    
    return v;
#endif
#endif
}

/**
 * Завершение процесса итерации 
 * 
 * @param s
 * @param v
 */
static void dpi_seq_stop(struct seq_file *s, void *v)  __releases(RCU) {
    pr_info("dpi_seq_stop(...) for v=0x%llx\n", (u64)v);
        
    if(v) {
        /* Освобождаем память итератора */
        kfree(v);

#if 0        
        struct dpi_conntrack_file *f;
        
        rcu_read_lock();
        
        /* Наша управляющая структура */
        f = rcu_dereference(s->private);
        
        pr_info("Destroy iterator for %s\n", f->name);
        
        rcu_read_unlock();
#endif        
    }
}

static int dpi_seq_show(struct seq_file *s, void *v) {
    if(v) {
        struct dpi_conntrack_file *f;
        
        rcu_read_lock();
        
        /* Наша управляющая структура */
        f = rcu_dereference(s->private);
        
        pr_info("dpi_seq_show(...) for %s\n", f->name);
        
        rcu_read_unlock();
    }
    
    return 0;
}

/**
 * Освобождение ресурсов после истечения RCU grace period
 * 
 * @param head
 */
static void dpi_conntrack_file_cleanup_rcu(struct rcu_head *head) {
    struct dpi_conntrack_file *f;

    f = container_of(head, struct dpi_conntrack_file, rcu);

    /* Удаляем файл с запрошенным именем - только по окончании работы с ним! */
    if(f->pde) {
        /* Удаляем элемент из procfs */
        proc_remove(f->pde);
        
        pr_info("dpi_conntrack_file_cleanup_rcu: Remove procfs net file\n");
        
        /* Освободить ресурсы */
        dpi_conntrack_file_free(f);
    }
}

/**
 * 
 * @param i
 * @param f
 * @param pos
 * @return 
 * 
 * NB!
 * Данный вызов всегда должен выполняться в окружении
 * rcu_read_lock();
 * rcu_read_unlock();
 */
static struct dpi_iterator *ct_get_idx(struct dpi_iterator *i, struct dpi_conntrack_file *f, loff_t pos) {
    /* Попытка найти первую не нулевую позицию */
    ct_get_first(i, f->net);
    
    pr_info("ct_get_first return head=0x%llx\n",(u64)i->head);
    
    while(i->head) {
        if(is_this_helper((struct nf_conntrack_tuple_hash *)i->head, f->name)) {
            if(!pos) {
                /* Найдена подходящая нам позиция */
                return i;
            }
            
            pos--;
        }
        
        /* Переходим к следующему connection */
        ct_get_next(i, f->net);
    }
  
    /* Больше нет подходящих нам позиций */
    return NULL;
}


/**
 * 
 * @param i
 * @param net
 * @return 
 * 
 * NB!
 * Данный вызов всегда должен выполняться в окружении
 * rcu_read_lock();
 * rcu_read_unlock();
 */
static void ct_get_next(struct dpi_iterator *i, struct net *net) {
    i->head = rcu_dereference(hlist_nulls_next_rcu(i->head));
    
    while(is_a_nulls(i->head)) {
        if (likely(get_nulls_value(i->head) == i->bucket)) {
            if (++i->bucket >= net->ct.htable_size) {
                i->head = NULL;
                
                break;
            }
        }

        i->head = rcu_dereference(hlist_nulls_first_rcu(&net->ct.hash[i->bucket]));
    }
}


/**
 * 
 * @param i
 * @param net
 * 
 * NB!
 * Данный вызов всегда должен выполняться в окружении
 * rcu_read_lock();
 * rcu_read_unlock();
 */
static void ct_get_first(struct dpi_iterator *i, struct net *net) {
    for(i->bucket = 0;i->bucket < net->ct.htable_size;i->bucket++) {
        i->head = rcu_dereference(hlist_nulls_first_rcu(&net->ct.hash[i->bucket]));
        if(!is_a_nulls(i->head)) {
            /* i->head != NULL */
            return;
        }
    }
    
    i->head = NULL;
}


/**
 * 
 * @param hash
 * @param name
 * @return 
 * 
 * NB!
 * Данный вызов всегда должен выполняться в окружении
 * rcu_read_lock();
 * rcu_read_unlock();
 */
static int is_this_helper(struct nf_conntrack_tuple_hash *hash, const char *name) {
    struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(hash);
    struct nf_conn_help *help = nfct_help(ct);
    
    pr_info("is_this_helper: ct=0x%llx help=0x%llx\n", (u64)ct, (u64)help);
    
    if(help) {
        struct nf_conntrack_helper __rcu *helper;
    
        helper = rcu_dereference(help->helper);

        if(helper) {
            pr_info("is_this_helper: %s %s\n", helper->name, name);
        }
        
        /* Сравниваем имя helper, если он имеется */
        return helper ? (strncmp(helper->name, name, NF_CT_HELPER_NAME_LEN) == 0) : 0;
    }
    
    return 0;
}
