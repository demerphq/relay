#ifndef _GRAPHITE_H
#define _GRAPHITE_H
/*
 From https://raw.githubusercontent.com/vdevos/graphite-c-client/master/graphite-client.h
 */

/* 
 * A simple pure C client for Graphite allows to send metrics to Graphite/Carbon using Graphite plaintext protocol
 */
#include <sys/types.h>

int graphite_init( const char *host, const int port );
void graphite_finalize( void );

/** 
  This function allows you to send metrics to Graphite/Carbon using the plaintext protocol
  @param path       this is the metric path - example: server.process.task.load, server.process.task.count, etc.
  @param value      this is the metric value - example: 1, 12.4, 113.0, etc.
  @param timestamp  this is your metrics timestamp (UNIX Epoch) - Warning: timestamp == 0 is also accepted!

  WARNING: Make sure you use graphite_init on port: 2003 (only this port allows the plaintext protocol)
*/
void graphite_send_plain( const char* path, float value, unsigned long timestamp );

#endif
