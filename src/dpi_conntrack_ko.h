/* 
 * File:   dpi_conntrack_ko.h
 * Author: monster
 *
 * Created on 16 июня 2017 г., 13:36
 */

#ifndef DPI_CONNTRACK_KO_H
#define DPI_CONNTRACK_KO_H

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <net/net_namespace.h>
#include <linux/spinlock.h>
#include <linux/hashtable.h>
#include <linux/rcupdate.h>


/* Public API */
#include "../include/dpi_conntrack.h"

/* Кол-во buckets в hashtable */
#define FILES_HASHTABLE_BITS 2

struct dpi_conntrack_net;

struct dpi_conntrack_file {
    /* Для хранения элемента в hashtable */
    struct hlist_node link;
    /* Для call_rcu, чтобы освобождать ресурс после истечения grace period */
    struct rcu_head rcu;
    /* Имя "Файла" */
    char *name;
    /* Мы также храним ссылку на netns (с увеличением счетчика использований!) */
    struct net *net;
  
    struct proc_dir_entry *pde;
};

/*
 * Область данных для реализуемой нами сетевой подсистеме, которая
 * имеется для каждой netns (struct net *)
 */
struct dpi_conntrack_net {
    /* Ссылка на каталог PROC_NET_DPI ("/proc/net/dpi") */
    struct proc_dir_entry *proc_dpi;
    
    /* Для доступа на запись к полю files */
    spinlock_t lock;
    /* hash-таблица для хранения структур dpi_conntrack_file */
    DECLARE_HASHTABLE(files, FILES_HASHTABLE_BITS);
    
};

/* netns.c */
int __init dpi_conntrack_netns_startup(void);
void dpi_conntrack_netns_cleanup(void);
struct dpi_conntrack_net *dpi_conntrack_pernet(struct net *net);

/* dpi_conntrack_file.c */
int dpi_conntrack_file_new_rcu(struct net *net,
                               struct dpi_conntrack_net *pernet, 
                               const char *name,
                               struct dpi_conntrack_file **df);
struct dpi_conntrack_file *dpi_conntrack_file_find_rcu(struct dpi_conntrack_net *pernet, 
                                                       const char *name);
void dpi_conntrack_file_free(struct dpi_conntrack_file *f);

/* procfs.c */
void dpi_conntrack_procfs_unregister_file(struct dpi_conntrack_file *f);

#endif /* DPI_CONNTRACK_KO_H */

