#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#include "bstring.h"
#include "server.h"
#include "message_header.h"
#include "eadd_message.h"
#include "next_action_message.h"
#include "aadd_message.h"
#include "aget_message.h"
#include "aall_message.h"
#include "padd_message.h"
#include "pget_message.h"
#include "pall_message.h"
#include "multi_message.h"
#include "dbg.h"


//==============================================================================
//
// Forward Declarations
//
//==============================================================================

int sky_server_open_table(sky_server *server, bstring database_name,
    bstring table_name, sky_table **table);

int sky_server_close_table(sky_server *server, sky_table *table);


//==============================================================================
//
// Functions
//
//==============================================================================

//--------------------------------------
// Lifecycle
//--------------------------------------

// Creates a reference to a server instance.
//
// path - The directory path where the databases reside.
//
// Returns a reference to the server.
sky_server *sky_server_create(bstring path)
{
    sky_server *server = NULL;
    server = calloc(1, sizeof(sky_server)); check_mem(server);
    server->path = bstrcpy(path);
    if(path) check_mem(server->path);
    server->port = SKY_DEFAULT_PORT;
    
    return server;

error:
    sky_server_free(server);
    return NULL;
}

// Frees a server instance from memory.
//
// server - The server object to free.
void sky_server_free(sky_server *server)
{
    if(server) {
        if(server->path) bdestroy(server->path);
        free(server);
    }
}


//--------------------------------------
// State
//--------------------------------------

// Starts a server. Once a server is started, it can accept messages over TCP
// on the bind address and port number specified by the server object.
//
// server - The server to start.
//
// Returns 0 if successful, otherwise returns -1.
int sky_server_start(sky_server *server)
{
    int rc;

    check(server != NULL, "Server required");
    check(server->state == SKY_SERVER_STATE_STOPPED, "Server already running");
    check(server->port > 0, "Port required");

    // Initialize socket info.
    server->sockaddr = calloc(1, sizeof(struct sockaddr_in));
    check_mem(server->sockaddr);
    server->sockaddr->sin_addr.s_addr = INADDR_ANY;
    server->sockaddr->sin_port = htons(server->port);
    server->sockaddr->sin_family = AF_INET;

    // Create socket.
    server->socket = socket(AF_INET, SOCK_STREAM, 0);
    check(server->socket != -1, "Unable to create a socket");
    
    // Set socket for reuse.
    int optval = 1;
    rc = setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    check(rc == 0, "Unable to set socket for reuse");
    
    // Bind socket.
    rc = bind(server->socket, (struct sockaddr*)server->sockaddr, sizeof(struct sockaddr_in));
    check(rc == 0, "Unable to bind socket");
    
    // Listen on socket.
    rc = listen(server->socket, SKY_LISTEN_BACKLOG);
    check(rc != -1, "Unable to listen on socket");
    
    // Update server state.
    server->state = SKY_SERVER_STATE_RUNNING;
    
    return 0;

error:
    sky_server_stop(server);
    return -1;
}

// Stops a server. This actions closes the TCP socket and in-process messages
// will be aborted.
//
// server - The server to stop.
//
// Returns 0 if successful, otherwise returns -1.
int sky_server_stop(sky_server *server)
{
    // Close socket if open.
    if(server->socket > 0) {
        close(server->socket);
    }
    server->socket = 0;

    // Clear socket info.
    if(server->sockaddr) {
        free(server->sockaddr);
    }
    server->sockaddr = NULL;
    
    // Update server state.
    server->state = SKY_SERVER_STATE_STOPPED;
    
    return 0;
}


//--------------------------------------
// Connection Management
//--------------------------------------

// Accepts a connection on a running server. Once a connection is accepted then
// the message is parsed and processed.
//
// server - The server.
//
// Returns 0 if successful, otherwise returns -1.
int sky_server_accept(sky_server *server)
{
    int rc;

    // Accept the next connection.
    int sockaddr_size = sizeof(struct sockaddr_in);
    int socket = accept(server->socket, (struct sockaddr*)server->sockaddr, (socklen_t *)&sockaddr_size);
    check(socket != -1, "Unable to accept connection");

    // Wrap socket in a buffered file reference.
    FILE *input = fdopen(socket, "r");
    FILE *output = fdopen(dup(socket), "w");
    check(input != NULL, "Unable to open buffered socket input");
    check(output != NULL, "Unable to open buffered socket output");
    
    // Process message.
    rc = sky_server_process_message(server, input, output);
    check(rc == 0, "Unable to process message");
    
    fclose(input);
    fclose(output);

    return 0;

error:
    fclose(input);
    fclose(output);
    return -1;
}


//--------------------------------------
// Message Processing
//--------------------------------------

// Processes a single message.
//
// server - The server.
// input  - The input stream.
// output - The output stream.
//
// Returns 0 if successful, otherwise returns -1.
int sky_server_process_message(sky_server *server, FILE *input, FILE *output)
{
    int rc;
    sky_message_header *header = NULL;
    
    // Parse message header.
    header = sky_message_header_create(); check_mem(header);
    rc = sky_message_header_unpack(header, input);
    check(rc == 0, "Unable to unpack message header");

    // Ignore the database/table if this is a multi message.
    if(biseqcstr(header->name, "multi") == 1) {
        rc = sky_server_process_multi_message(server, input, output);
        check(rc == 0, "Unable to process multi message");
    }
    else {
        // Open database & table.
        sky_table *table = NULL;
        rc = sky_server_open_table(server, header->database_name, header->table_name, &table);
        check(rc == 0, "Unable to open table");

        // Parse appropriate message type.
        if(biseqcstr(header->name, "eadd") == 1) {
            rc = sky_server_process_eadd_message(server, table, input, output);
        }
        else if(biseqcstr(header->name, "next_action") == 1) {
            rc = sky_server_process_next_action_message(server, table, input, output);
        }
        else if(biseqcstr(header->name, "aadd") == 1) {
            rc = sky_server_process_aadd_message(server, table, input, output);
        }
        else if(biseqcstr(header->name, "aget") == 1) {
            rc = sky_server_process_aget_message(server, table, input, output);
        }
        else if(biseqcstr(header->name, "aall") == 1) {
            rc = sky_server_process_aall_message(server, table, input, output);
        }
        else if(biseqcstr(header->name, "padd") == 1) {
            rc = sky_server_process_padd_message(server, table, input, output);
        }
        else if(biseqcstr(header->name, "pget") == 1) {
            rc = sky_server_process_pget_message(server, table, input, output);
        }
        else if(biseqcstr(header->name, "pall") == 1) {
            rc = sky_server_process_pall_message(server, table, input, output);
        }
        else {
            sentinel("Invalid message type");
        }
        check(rc == 0, "Unable to process message: %s", bdata(header->name));
    }
    
    sky_message_header_free(header);
    return 0;

error:
    sky_message_header_free(header);
    return -1;
}


//--------------------------------------
// Table management
//--------------------------------------

// Opens an table.
//
// server        - The server that is opening the table.
// database_name - The name of the database to open.
// table_name    - The name of the table to open.
// table         - Returns the instance of the table to the caller. 
//
// Returns 0 if successful, otherwise returns -1.
int sky_server_open_table(sky_server *server, bstring database_name,
                          bstring table_name,  sky_table **table)
{
    int rc;
    check(server != NULL, "Server required");
    check(blength(database_name) > 0, "Database name required");
    check(blength(table_name) > 0, "Table name required");
    
    // Initialize return values.
    *table = NULL;
    
    // Determine the path to the table.
    bstring path = bformat("%s/%s/%s", bdata(server->path), bdata(database_name), bdata(table_name));
    check_mem(path);

    // If the table is already open then reuse it.
    if(server->last_table != NULL && biseq(server->last_table->path, path) == 1) {
        *table = server->last_table;
    }
    // Otherwise open the table.
    else {
        // Close the currently open table if one exists
        if(server->last_table != NULL) {
            rc = sky_server_close_table(server, server->last_table);
            check(rc == 0, "Unable to close current table");
        }
        
        // Create the table.
        *table = sky_table_create(); check_mem(*table);
        rc = sky_table_set_path(*table, path);
        check(rc == 0, "Unable to set table path");
    
        // Open the table.
        rc = sky_table_open(*table);
        check(rc == 0, "Unable to open table");
    
        // Save the reference as the currently open table.
        server->last_table = *table;
    }

    bdestroy(path);
    return 0;

error:
    bdestroy(path);
    sky_table_free(*table);
    *table = NULL;
    return -1;
}

// Closes a table.
//
// server - The server.
// table  - The table to close.
//
// Returns 0 if successful, otherwise returns -1.
int sky_server_close_table(sky_server *server, sky_table *table)
{
    int rc;
    check(server != NULL, "Server required");
    check(table != NULL, "Table required");
    
    // Close the table.
    rc = sky_table_close(table);
    check(rc == 0, "Unable to close table");

    // Free the table.
    sky_table_free(table);
    
    return 0;

error:
    sky_table_free(table);
    return -1;
}


//--------------------------------------
// Event Messages
//--------------------------------------

// Parses and process an Event Add (EADD) message.
//
// server - The server.
// table  - The table to apply the message to.
// input  - The input file stream.
// output - The output file stream.
//
// Returns 0 if successful, otherwise returns -1.
int sky_server_process_eadd_message(sky_server *server, sky_table *table,
                                    FILE *input, FILE *output)
{
    int rc;
    check(server != NULL, "Server required");
    check(table != NULL, "Table required");
    check(input != NULL, "Input required");
    check(output != NULL, "Output stream required");
    
    debug("Message received: [EADD]");
    
    // Parse message.
    sky_eadd_message *message = sky_eadd_message_create(); check_mem(message);
    rc = sky_eadd_message_unpack(message, input);
    check(rc == 0, "Unable to parse EADD message");
    
    // Process message.
    rc = sky_eadd_message_process(message, table, output);
    check(rc == 0, "Unable to process EADD message");
    
    return 0;

error:
    return -1;
}


//--------------------------------------
// Query Messages
//--------------------------------------

// Parses and process a 'Next Action' message.
//
// server - The server.
// table  - The table to apply the message to.
// input  - The input file stream.
// output - The output file stream.
//
// Returns 0 if successful, otherwise returns -1.
int sky_server_process_next_action_message(sky_server *server, sky_table *table,
                                           FILE *input, FILE *output)
{
    int rc;
    check(server != NULL, "Server required");
    check(table != NULL, "Table required");
    check(input != NULL, "Input required");
    check(output != NULL, "Output stream required");
    
    debug("Message received: [Next Action]");
    
    // Parse message.
    sky_next_action_message *message = sky_next_action_message_create(); check_mem(message);
    rc = sky_next_action_message_unpack(message, input);
    check(rc == 0, "Unable to parse 'Next Action' message");
    
    // Process message.
    rc = sky_next_action_message_process(message, table, output);
    check(rc == 0, "Unable to process 'Next Action' message");
    
    return 0;

error:
    return -1;
}



//--------------------------------------
// Action Messages
//--------------------------------------

// Parses and process an Action-Add (AADD) message.
//
// server - The server.
// table  - The table to apply the message to.
// input  - The input file stream.
// output - The output file stream.
//
// Returns 0 if successful, otherwise returns -1.
int sky_server_process_aadd_message(sky_server *server, sky_table *table,
                                    FILE *input, FILE *output)
{
    int rc;
    check(server != NULL, "Server required");
    check(table != NULL, "Table required");
    check(input != NULL, "Input required");
    check(output != NULL, "Output stream required");
    
    debug("Message received: [AADD]");

    // Parse message.
    sky_aadd_message *message = sky_aadd_message_create(); check_mem(message);
    rc = sky_aadd_message_unpack(message, input);
    check(rc == 0, "Unable to parse AADD message");
    
    // Process message.
    rc = sky_aadd_message_process(message, table, output);
    check(rc == 0, "Unable to process AADD message");
    
    return 0;

error:
    return -1;
}

// Parses and process an Action-Get (AGET) message.
//
// server - The server.
// table  - The table to apply the message to.
// input  - The input file stream.
// output - The output file stream.
//
// Returns 0 if successful, otherwise returns -1.
int sky_server_process_aget_message(sky_server *server, sky_table *table,
                                    FILE *input, FILE *output)
{
    int rc;
    check(server != NULL, "Server required");
    check(table != NULL, "Table required");
    check(input != NULL, "Input required");
    check(output != NULL, "Output stream required");
    
    debug("Message received: [AGET]");

    // Parse message.
    sky_aget_message *message = sky_aget_message_create(); check_mem(message);
    rc = sky_aget_message_unpack(message, input);
    check(rc == 0, "Unable to parse AGET message");
    
    // Process message.
    rc = sky_aget_message_process(message, table, output);
    check(rc == 0, "Unable to process AGET message");
    
    return 0;

error:
    return -1;
}

// Parses and process an Action-All (AALL) message.
//
// server - The server.
// table  - The table to apply the message to.
// input  - The input file stream.
// output - The output file stream.
//
// Returns 0 if successful, otherwise returns -1.
int sky_server_process_aall_message(sky_server *server, sky_table *table,
                                    FILE *input, FILE *output)
{
    int rc;
    check(server != NULL, "Server required");
    check(table != NULL, "Table required");
    check(input != NULL, "Input required");
    check(output != NULL, "Output stream required");
    
    debug("Message received: [AALL]");

    // Parse message.
    sky_aall_message *message = sky_aall_message_create(); check_mem(message);
    rc = sky_aall_message_unpack(message, input);
    check(rc == 0, "Unable to parse AALL message");
    
    // Process message.
    rc = sky_aall_message_process(message, table, output);
    check(rc == 0, "Unable to process AALL message");
    
    return 0;

error:
    return -1;
}


//--------------------------------------
// Property Messages
//--------------------------------------

// Parses and process an Property-Add (AADD) message.
//
// server - The server.
// table  - The table to apply the message to.
// input  - The input file stream.
// output - The output file stream.
//
// Returns 0 if successful, otherwise returns -1.
int sky_server_process_padd_message(sky_server *server, sky_table *table,
                                    FILE *input, FILE *output)
{
    int rc;
    check(server != NULL, "Server required");
    check(table != NULL, "Table required");
    check(input != NULL, "Input required");
    check(output != NULL, "Output stream required");
    
    debug("Message received: [PADD]");

    // Parse message.
    sky_padd_message *message = sky_padd_message_create(); check_mem(message);
    rc = sky_padd_message_unpack(message, input);
    check(rc == 0, "Unable to parse PADD message");
    
    // Process message.
    rc = sky_padd_message_process(message, table, output);
    check(rc == 0, "Unable to process PADD message");
    
    return 0;

error:
    return -1;
}

// Parses and process an Property-Get (PGET) message.
//
// server - The server.
// table  - The table to apply the message to.
// input  - The input file stream.
// output - The output file stream.
//
// Returns 0 if successful, otherwise returns -1.
int sky_server_process_pget_message(sky_server *server, sky_table *table,
                                    FILE *input, FILE *output)
{
    int rc;
    check(server != NULL, "Server required");
    check(table != NULL, "Table required");
    check(input != NULL, "Input required");
    check(output != NULL, "Output stream required");
    
    debug("Message received: [PGET]");

    // Parse message.
    sky_pget_message *message = sky_pget_message_create(); check_mem(message);
    rc = sky_pget_message_unpack(message, input);
    check(rc == 0, "Unable to parse PGET message");
    
    // Process message.
    rc = sky_pget_message_process(message, table, output);
    check(rc == 0, "Unable to process PGET message");
    
    return 0;

error:
    return -1;
}

// Parses and process an Property-All (PALL) message.
//
// server - The server.
// table  - The table to apply the message to.
// input  - The input file stream.
// output - The output file stream.
//
// Returns 0 if successful, otherwise returns -1.
int sky_server_process_pall_message(sky_server *server, sky_table *table,
                                    FILE *input, FILE *output)
{
    int rc;
    check(server != NULL, "Server required");
    check(table != NULL, "Table required");
    check(input != NULL, "Input required");
    check(output != NULL, "Output stream required");
    
    debug("Message received: [PALL]");

    // Parse message.
    sky_pall_message *message = sky_pall_message_create(); check_mem(message);
    rc = sky_pall_message_unpack(message, input);
    check(rc == 0, "Unable to parse PALL message");
    
    // Process message.
    rc = sky_pall_message_process(message, table, output);
    check(rc == 0, "Unable to process PALL message");
    
    return 0;

error:
    return -1;
}


//--------------------------------------
// Multi Message
//--------------------------------------

// Parses and process a multi message.
//
// server - The server.
// input  - The input file stream.
// output - The output file stream.
//
// Returns 0 if successful, otherwise returns -1.
int sky_server_process_multi_message(sky_server *server, FILE *input,
                                     FILE *output)
{
    int rc;
    check(server != NULL, "Server required");
    check(input != NULL, "Input required");
    check(output != NULL, "Output stream required");
    
    debug("Message received: [MULTI]");
    
    // Parse message.
    sky_multi_message *message = sky_multi_message_create(); check_mem(message);
    rc = sky_multi_message_unpack(message, input);
    check(rc == 0, "Unable to parse MULTI message");

    // Start time.
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t t0 = (tv.tv_sec*1000) + (tv.tv_usec/1000);

    // Process message.
    uint32_t i;
    for(i=0; i<message->message_count; i++) {
        rc = sky_server_process_message(server, input, output);
        check(rc == 0, "Unable to process child message");
    }

    // End time.
    gettimeofday(&tv, NULL);
    int64_t t1 = (tv.tv_sec*1000) + (tv.tv_usec/1000);
    printf("MULTI: %d messages processed in %.3f seconds\n", message->message_count, ((float)(t1-t0))/1000);
    
    return 0;

error:
    return -1;
}
