/* 
 * File:   dpi_conntrack.h
 * Author: monster
 *
 * Created on 16 июня 2017 г., 14:54
 */

#ifndef DPI_CONNTRACK_H
#define DPI_CONNTRACK_H

#include <net/net_namespace.h>

int dpi_conntrack_register_file(const char *name, struct net *net);
int dpi_conntrack_unregister_file(const char *name, struct net *net);

#endif /* DPI_CONNTRACK_H */

