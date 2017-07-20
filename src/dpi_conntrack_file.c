#include <linux/crc32.h>
#include <linux/rcupdate.h>

#include "dpi_conntrack_ko.h"

/* seed для вычисления hash на основе CRC32 */
#define HASH_CRC_SEED   0xEDB88320

/* Предварительное объявление локальных функций модуля */
static struct dpi_conntrack_file *file_find_rcu(struct dpi_conntrack_net *pernet, 
                                                const char *name, size_t len, 
                                                u32 hash);
/**
 * Вычисление hash-значения по ASCIZ-строке
 * 
 * @param file
 * @return 
 */
static inline u32 string_hash(const char *s, size_t len) {
    return crc32(HASH_CRC_SEED, s, len);
}


/**
 * Создать новый элемент только в том случае, если он еще не существует
 * 
 * @param net
 * @param pernet
 * @param name
 * @return 
 * 
 * Данный вызов всегда должен выполняться в окружении
 * rcu_read_lock();
 * rcu_read_unlock();
 */
int dpi_conntrack_file_new_rcu(struct net *net,
                               struct dpi_conntrack_net *pernet, 
                               const char *name,
                               struct dpi_conntrack_file **df) {
    size_t len = strlen(name);
    u32 hash = string_hash(name, len);
    
    /* Попытаться найти существующий элемент */
    struct dpi_conntrack_file *f = file_find_rcu(pernet, name, len, hash);
    
    if(f) {
        /* Был найден существующий элемент, это ошибка! */
        return -EEXIST;
    }

    /* Элемента с таким именем не существует, создаем новый */
    f = kzalloc(sizeof(struct dpi_conntrack_file), GFP_ATOMIC);
    
    if(NULL == f) {
        /* Память не выделена */
        return -ENOMEM;
    }
    
    /* Выделяем память для хранения имени */
    f->name = kmalloc(len + 1, GFP_ATOMIC);
    
    if(NULL == f->name) {
        /* Память не выделена */
        kfree(f);
        
        return -ENOMEM;
    }
    
    /* Копируем имя */
    memcpy(f->name, name, len);

    /* Ограничитель имени */
    *(f->name + len) = 0;

    /* В данный момент элемент еще не находится в таблице */
    INIT_HLIST_NODE(&f->link);
    
    /* Ссылка на netns (с увеличением кол-ва использований) */
    f->net = get_net(net);
    
    /* Доступ на запись к pernet->files */
    spin_lock(&pernet->lock);

    /* Добавляем вновь созданный элемент в таблицу */
    hash_add_rcu(pernet->files, &f->link, hash);
    
    /* Окончание доступа на запись к pernet->files */
    spin_unlock(&pernet->lock);
    
    /* Возвращаем вновь созданный и добавленный в hash-таблицу элемент */
    rcu_assign_pointer(*df, f);
    
    /* Ошибок не обнаружено */
    return 0;
}

/**
 * Освобождение ресурсов
 * 
 * @param f
 */
void dpi_conntrack_file_free(struct dpi_conntrack_file *f) {
    /* Нет ссылки на netns */
    put_net(f->net);
    
    /* Освобождаем память из-под имени "файла" */
    kfree(f->name);
    
    /* Освобождаем память от данной структуры */
    kfree(f);
}

/**
 * 
 * @param pernet
 * @param name
 * @return 
 * 
 * Данный вызов всегда должен выполняться в окружении
 * rcu_read_lock();
 * rcu_read_unlock();
 */
struct dpi_conntrack_file *dpi_conntrack_file_find_rcu(struct dpi_conntrack_net *pernet, 
                                                       const char *name) {
    size_t len = strlen(name);
    u32 hash = string_hash(name, len);
    
    /* Попытаться найти существующий элемент */
    return file_find_rcu(pernet, name, len, hash);
}


/**
 * Реализация поиска значения в hash-таблице
 * 
 * @param pernet
 * @param name
 * @param len
 * @param hash
 * @return 
 * 
 * Данный вызов всегда должен выполняться в окружении
 * rcu_read_lock();
 * rcu_read_unlock();
 */
static struct dpi_conntrack_file *file_find_rcu(struct dpi_conntrack_net *pernet, 
                                                const char *name, size_t len, 
                                                u32 hash) {
    struct dpi_conntrack_file *f;
    
    /* Выборка из таблицы по совпадению hash-значения */
    hash_for_each_possible_rcu(pernet->files, f, link, hash) {
        /* Проверяем на точное совпадение длины имени */
        if(strlen(f->name) == len) {
            /* Проверяем на точное совпадение имени */
            if(0 == memcmp(f->name, name, len)) {
                /* Возвращаем уже существующий элемент */
                return f;
            }
        }
    }
    
    return NULL;
}
