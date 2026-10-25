#ifndef _SERVER_HEALTH_H
#define _SERVER_HEALTH_H

/* starts the endpoint on the port (0: off); called once the host is set up */
void server_health_start(short port);

/* publishes the current state for the endpoint to serve; called every frame
   from network_test_update() while hosting */
void server_health_publish(void);

/* stops the endpoint (socket closed, thread told to exit) */
void server_health_stop(void);

#endif /* _SERVER_HEALTH_H */
