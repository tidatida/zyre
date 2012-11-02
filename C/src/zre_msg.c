/*  =========================================================================
    zre_msg.c

    Generated codec implementation for zre_msg
    -------------------------------------------------------------------------
    Copyright (c) 1991-2012 iMatix Corporation -- http://www.imatix.com     
    Copyright other contributors as noted in the AUTHORS file.              
                                                                            
    This file is part of FILEMQ, see http://filemq.org.                     
                                                                            
    This is free software; you can redistribute it and/or modify it under   
    the terms of the GNU Lesser General Public License as published by the  
    Free Software Foundation; either version 3 of the License, or (at your  
    option) any later version.                                              
                                                                            
    This software is distributed in the hope that it will be useful, but    
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTA-   
    BILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General  
    Public License for more details.                                        
                                                                            
    You should have received a copy of the GNU Lesser General Public License
    along with this program. If not, see http://www.gnu.org/licenses/.      
    =========================================================================
*/

#include <czmq.h>
#include "../include/zre_msg.h"

//  Structure of our class

struct _zre_msg_t {
    zframe_t *address;          //  Address of peer if any
    int id;                     //  zre_msg message ID
    byte *needle;               //  Read/write pointer for serialization
    byte *ceiling;              //  Valid upper limit for read pointer
    uint32_t sequence;          //  Message sequence
    char *from;
    byte port;
    zlist_t *groups;
    byte status;
    zhash_t *headers;
    size_t headers_bytes;       //  Size of dictionary content
    zframe_t *cookies;
    char *group;
};

//  --------------------------------------------------------------------------
//  Network data encoding macros

//  Strings are encoded with 1-byte length
#define STRING_MAX  255

//  Put a block to the frame
#define PUT_BLOCK(host,size) { \
    memcpy (self->needle, (host), size); \
    self->needle += size; \
    }

//  Get a block from the frame
#define GET_BLOCK(host,size) { \
    if (self->needle + size > self->ceiling) \
        goto malformed; \
    memcpy ((host), self->needle, size); \
    self->needle += size; \
    }

//  Put a 1-byte number to the frame
#define PUT_NUMBER1(host) { \
    *(byte *) self->needle = (host); \
    self->needle++; \
    }

//  Put a 2-byte number to the frame
#define PUT_NUMBER2(host) { \
    self->needle [0] = (byte) (((host) >> 8)  & 255); \
    self->needle [1] = (byte) (((host))       & 255); \
    self->needle += 2; \
    }

//  Put a 4-byte number to the frame
#define PUT_NUMBER4(host) { \
    self->needle [0] = (byte) (((host) >> 24) & 255); \
    self->needle [1] = (byte) (((host) >> 16) & 255); \
    self->needle [2] = (byte) (((host) >> 8)  & 255); \
    self->needle [3] = (byte) (((host))       & 255); \
    self->needle += 4; \
    }

//  Put a 8-byte number to the frame
#define PUT_NUMBER8(host) { \
    self->needle [0] = (byte) (((host) >> 56) & 255); \
    self->needle [1] = (byte) (((host) >> 48) & 255); \
    self->needle [2] = (byte) (((host) >> 40) & 255); \
    self->needle [3] = (byte) (((host) >> 32) & 255); \
    self->needle [4] = (byte) (((host) >> 24) & 255); \
    self->needle [5] = (byte) (((host) >> 16) & 255); \
    self->needle [6] = (byte) (((host) >> 8)  & 255); \
    self->needle [7] = (byte) (((host))       & 255); \
    self->needle += 8; \
    }

//  Get a 1-byte number from the frame
#define GET_NUMBER1(host) { \
    if (self->needle + 1 > self->ceiling) \
        goto malformed; \
    (host) = *(byte *) self->needle; \
    self->needle++; \
    }

//  Get a 2-byte number from the frame
#define GET_NUMBER2(host) { \
    if (self->needle + 2 > self->ceiling) \
        goto malformed; \
    (host) = ((int16_t) (self->needle [0]) << 8) \
           +  (int16_t) (self->needle [1]); \
    self->needle += 2; \
    }

//  Get a 4-byte number from the frame
#define GET_NUMBER4(host) { \
    if (self->needle + 4 > self->ceiling) \
        goto malformed; \
    (host) = ((int32_t) (self->needle [0]) << 24) \
           + ((int32_t) (self->needle [1]) << 16) \
           + ((int32_t) (self->needle [2]) << 8) \
           +  (int32_t) (self->needle [3]); \
    self->needle += 4; \
    }

//  Get a 8-byte number from the frame
#define GET_NUMBER8(host) { \
    if (self->needle + 8 > self->ceiling) \
        goto malformed; \
    (host) = ((int64_t) (self->needle [0]) << 56) \
           + ((int64_t) (self->needle [1]) << 48) \
           + ((int64_t) (self->needle [2]) << 40) \
           + ((int64_t) (self->needle [3]) << 32) \
           + ((int64_t) (self->needle [4]) << 24) \
           + ((int64_t) (self->needle [5]) << 16) \
           + ((int64_t) (self->needle [6]) << 8) \
           +  (int64_t) (self->needle [7]); \
    self->needle += 8; \
    }

//  Put a string to the frame
#define PUT_STRING(host) { \
    string_size = strlen (host); \
    PUT_NUMBER1 (string_size); \
    memcpy (self->needle, (host), string_size); \
    self->needle += string_size; \
    }

//  Get a string from the frame
#define GET_STRING(host) { \
    GET_NUMBER1 (string_size); \
    if (self->needle + string_size > (self->ceiling)) \
        goto malformed; \
    (host) = (char *) malloc (string_size + 1); \
    memcpy ((host), self->needle, string_size); \
    (host) [string_size] = 0; \
    self->needle += string_size; \
    }


//  --------------------------------------------------------------------------
//  Create a new zre_msg

zre_msg_t *
zre_msg_new (int id)
{
    zre_msg_t *self = (zre_msg_t *) zmalloc (sizeof (zre_msg_t));
    self->id = id;
    self->sequence = 0;
    return self;
}


//  --------------------------------------------------------------------------
//  Destroy the zre_msg

void
zre_msg_destroy (zre_msg_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        zre_msg_t *self = *self_p;

        //  Free class properties
        zframe_destroy (&self->address);
        free (self->from);
        if (self->groups)
            zlist_destroy (&self->groups);
        zhash_destroy (&self->headers);
        zframe_destroy (&self->cookies);
        free (self->group);

        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}


//  --------------------------------------------------------------------------
//  Receive and parse a zre_msg from the socket. Returns new object or
//  NULL if error. Will block if there's no message waiting.

zre_msg_t *
zre_msg_recv (void *socket)
{
    assert (socket);
    zre_msg_t *self = zre_msg_new (0);
    zframe_t *frame = NULL;

    //  If we're reading from a ROUTER socket, get address
    if (zsockopt_type (socket) == ZMQ_ROUTER) {
        self->address = zframe_recv (socket);
        if (!self->address)
            goto empty;         //  Interrupted
        if (!zsocket_rcvmore (socket))
            goto malformed;
    }
    //  Read and parse command in frame
    frame = zframe_recv (socket);
    if (!frame)
        goto empty;             //  Interrupted
    self->needle = zframe_data (frame);
    self->ceiling = self->needle + zframe_size (frame);
    size_t string_size;
    size_t list_size;
    size_t hash_size;

    //  Get message id, which is first byte in frame
    GET_NUMBER1 (self->id);
    //  Get message sequence, which is next 4 bytes in frame
    GET_NUMBER4 (self->sequence);

    switch (self->id) {
        case ZRE_MSG_HELLO:
            free (self->from);
            GET_STRING (self->from);
            GET_NUMBER2 (self->port);
            GET_NUMBER1 (list_size);
            self->groups = zlist_new ();
            zlist_autofree (self->groups);
            while (list_size--) {
                char *string;
                GET_STRING (string);
                zlist_append (self->groups, string);
            }
            GET_NUMBER1 (self->status);
            GET_NUMBER1 (hash_size);
            self->headers = zhash_new ();
            while (hash_size--) {
                char *string;
                GET_STRING (string);
                char *value = strchr (string, '=');
                if (value)
                    *value++ = 0;
                zhash_insert (self->headers, string, strdup (value));
                zhash_freefn (self->headers, string, free);
                free (string);
            }
            break;

        case ZRE_MSG_WHISPER:
            //  Get next frame, leave current untouched
            if (!zsocket_rcvmore (socket))
                goto malformed;
            self->cookies = zframe_recv (socket);
            break;

        case ZRE_MSG_SHOUT:
            free (self->group);
            GET_STRING (self->group);
            //  Get next frame, leave current untouched
            if (!zsocket_rcvmore (socket))
                goto malformed;
            self->cookies = zframe_recv (socket);
            break;

        case ZRE_MSG_JOIN:
            free (self->group);
            GET_STRING (self->group);
            GET_NUMBER1 (self->status);
            break;

        case ZRE_MSG_LEAVE:
            free (self->group);
            GET_STRING (self->group);
            GET_NUMBER1 (self->status);
            break;

        case ZRE_MSG_PING:
            break;

        case ZRE_MSG_PING_OK:
            break;

        default:
            goto malformed;
    }
    //  Successful return
    zframe_destroy (&frame);
    return self;

    //  Error returns
    malformed:
        printf ("E: malformed message '%d'\n", self->id);
    empty:
        zframe_destroy (&frame);
        zre_msg_destroy (&self);
        return (NULL);
}

//  Count size of key=value pair
static int
s_headers_count (const char *key, void *item, void *argument)
{
    zre_msg_t *self = (zre_msg_t *) argument;
    self->headers_bytes += strlen (key) + 1 + strlen ((char *) item) + 1;
    return 0;
}

//  Serialize headers key=value pair
static int
s_headers_write (const char *key, void *item, void *argument)
{
    zre_msg_t *self = (zre_msg_t *) argument;
    char string [STRING_MAX + 1];
    snprintf (string, STRING_MAX, "%s=%s", key, (char *) item);
    size_t string_size;
    PUT_STRING (string);
    return 0;
}


//  --------------------------------------------------------------------------
//  Send the zre_msg to the socket, and destroy it
//  Returns 0 if OK, else -1

int
zre_msg_send (zre_msg_t **self_p, void *socket, uint32_t sequence)
{
    assert (socket);
    assert (self_p);
    assert (*self_p);

    //  Calculate size of serialized data
    zre_msg_t *self = *self_p;
    size_t frame_size = 1;
    self->sequence = sequence;
    frame_size += 4;    // sequence size
    switch (self->id) {
        case ZRE_MSG_HELLO:
            //  from is a string with 1-byte length
            frame_size++;       //  Size is one octet
            if (self->from)
                frame_size += strlen (self->from);
            //  port is a 2-byte integer
            frame_size += 2;
            //  groups is an array of strings
            frame_size++;       //  Size is one octet
            if (self->groups) {
                //  Add up size of list contents
                char *groups = (char *) zlist_first (self->groups);
                while (groups) {
                    frame_size += 1 + strlen (groups);
                    groups = (char *) zlist_next (self->groups);
                }
            }
            //  status is a 1-byte integer
            frame_size += 1;
            //  headers is an array of key=value strings
            frame_size++;       //  Size is one octet
            if (self->headers) {
                self->headers_bytes = 0;
                //  Add up size of dictionary contents
                zhash_foreach (self->headers, s_headers_count, self);
            }
            frame_size += self->headers_bytes;
            break;
            
        case ZRE_MSG_WHISPER:
            break;
            
        case ZRE_MSG_SHOUT:
            //  group is a string with 1-byte length
            frame_size++;       //  Size is one octet
            if (self->group)
                frame_size += strlen (self->group);
            break;
            
        case ZRE_MSG_JOIN:
            //  group is a string with 1-byte length
            frame_size++;       //  Size is one octet
            if (self->group)
                frame_size += strlen (self->group);
            //  status is a 1-byte integer
            frame_size += 1;
            break;
            
        case ZRE_MSG_LEAVE:
            //  group is a string with 1-byte length
            frame_size++;       //  Size is one octet
            if (self->group)
                frame_size += strlen (self->group);
            //  status is a 1-byte integer
            frame_size += 1;
            break;
            
        case ZRE_MSG_PING:
            break;
            
        case ZRE_MSG_PING_OK:
            break;
            
        default:
            printf ("E: bad message type '%d', not sent\n", self->id);
            //  No recovery, this is a fatal application error
            assert (false);
    }
    //  Now serialize message into the frame
    zframe_t *frame = zframe_new (NULL, frame_size + 1);
    self->needle = zframe_data (frame);
    size_t string_size;
    int frame_flags = 0;
    PUT_NUMBER1 (self->id);
    PUT_NUMBER4 (self->sequence);

    switch (self->id) {
        case ZRE_MSG_HELLO:
            if (self->from) {
                PUT_STRING (self->from);
            }
            else
                PUT_NUMBER1 (0);    //  Empty string
            PUT_NUMBER2 (self->port);
            if (self->groups != NULL) {
                PUT_NUMBER1 (zlist_size (self->groups));
                char *groups = (char *) zlist_first (self->groups);
                while (groups) {
                    PUT_STRING (groups);
                    groups = (char *) zlist_next (self->groups);
                }
            }
            else
                PUT_NUMBER1 (0);    //  Empty string array
            PUT_NUMBER1 (self->status);
            if (self->headers != NULL) {
                PUT_NUMBER1 (zhash_size (self->headers));
                zhash_foreach (self->headers, s_headers_write, self);
            }
            else
                PUT_NUMBER1 (0);    //  Empty dictionary
            break;
            
        case ZRE_MSG_WHISPER:
            frame_flags = ZFRAME_MORE;
            break;
            
        case ZRE_MSG_SHOUT:
            if (self->group) {
                PUT_STRING (self->group);
            }
            else
                PUT_NUMBER1 (0);    //  Empty string
            frame_flags = ZFRAME_MORE;
            break;
            
        case ZRE_MSG_JOIN:
            if (self->group) {
                PUT_STRING (self->group);
            }
            else
                PUT_NUMBER1 (0);    //  Empty string
            PUT_NUMBER1 (self->status);
            break;
            
        case ZRE_MSG_LEAVE:
            if (self->group) {
                PUT_STRING (self->group);
            }
            else
                PUT_NUMBER1 (0);    //  Empty string
            PUT_NUMBER1 (self->status);
            break;
            
        case ZRE_MSG_PING:
            break;
            
        case ZRE_MSG_PING_OK:
            break;
            
    }
    //  If we're sending to a ROUTER, we send the address first
    if (zsockopt_type (socket) == ZMQ_ROUTER) {
        assert (self->address);
        if (zframe_send (&self->address, socket, ZFRAME_MORE))
            return -1;
    }
    //  Now send the data frame
    if (zframe_send (&frame, socket, frame_flags)) {
        zframe_destroy (&frame);
        return -1;
    }
    
    //  Now send any frame fields, in order
    switch (self->id) {
        case ZRE_MSG_WHISPER:
            //  If cookies isn't set, send an empty frame
            if (!self->cookies)
                self->cookies = zframe_new (NULL, 0);
            if (zframe_send (&self->cookies, socket, 0)) {
                zframe_destroy (&frame);
                return -1;
            }
            break;
        case ZRE_MSG_SHOUT:
            //  If cookies isn't set, send an empty frame
            if (!self->cookies)
                self->cookies = zframe_new (NULL, 0);
            if (zframe_send (&self->cookies, socket, 0)) {
                zframe_destroy (&frame);
                return -1;
            }
            break;
    }
    //  Destroy zre_msg object
    zre_msg_destroy (self_p);
    return 0;
}


//  --------------------------------------------------------------------------
//  Duplicate the zre_msg message

zre_msg_t *
zre_msg_dup (zre_msg_t *self)
{
    if (!self)
        return NULL;
        
    zre_msg_t *copy = zre_msg_new (self->id);
    copy->sequence = self->sequence;
    if (self->address)
        copy->address = zframe_dup (self->address);
    switch (self->id) {
        case ZRE_MSG_HELLO:
            copy->from = strdup (self->from);
            copy->port = self->port;
            copy->groups = zlist_copy (self->groups);
            copy->status = self->status;
            copy->headers = zhash_dup (self->headers);
            break;

        case ZRE_MSG_WHISPER:
            copy->cookies = zframe_dup (self->cookies);
            break;

        case ZRE_MSG_SHOUT:
            copy->group = strdup (self->group);
            copy->cookies = zframe_dup (self->cookies);
            break;

        case ZRE_MSG_JOIN:
            copy->group = strdup (self->group);
            copy->status = self->status;
            break;

        case ZRE_MSG_LEAVE:
            copy->group = strdup (self->group);
            copy->status = self->status;
            break;

        case ZRE_MSG_PING:
            break;

        case ZRE_MSG_PING_OK:
            break;

    }
    return copy;
}


//  Dump headers key=value pair to stdout
int
s_headers_dump (const char *key, void *item, void *argument)
{
    zre_msg_t *self = (zre_msg_t *) argument;
    printf ("        %s=%s\n", key, (char *) item);
    return 0;
}


//  --------------------------------------------------------------------------
//  Print contents of message to stdout

void
zre_msg_dump (zre_msg_t *self)
{
    assert (self);
    switch (self->id) {
        case ZRE_MSG_HELLO:
            puts ("HELLO:");
            if (self->from)
                printf ("    from='%s'\n", self->from);
            else
                printf ("    from=\n");
            printf ("    port=%ld\n", (long) self->port);
            printf ("    groups={");
            if (self->groups) {
                char *groups = (char *) zlist_first (self->groups);
                while (groups) {
                    printf (" '%s'", groups);
                    groups = (char *) zlist_next (self->groups);
                }
            }
            printf (" }\n");
            printf ("    status=%ld\n", (long) self->status);
            printf ("    headers={\n");
            if (self->headers)
                zhash_foreach (self->headers, s_headers_dump, self);
            printf ("    }\n");
            break;
            
        case ZRE_MSG_WHISPER:
            puts ("WHISPER:");
            printf ("    cookies={\n");
            if (self->cookies) {
                size_t size = zframe_size (self->cookies);
                byte *data = zframe_data (self->cookies);
                printf ("        size=%td\n", zframe_size (self->cookies));
                if (size > 32)
                    size = 32;
                int cookies_index;
                for (cookies_index = 0; cookies_index < size; cookies_index++) {
                    if (cookies_index && (cookies_index % 4 == 0))
                        printf ("-");
                    printf ("%02X", data [cookies_index]);
                }
            }
            printf ("    }\n");
            break;
            
        case ZRE_MSG_SHOUT:
            puts ("SHOUT:");
            if (self->group)
                printf ("    group='%s'\n", self->group);
            else
                printf ("    group=\n");
            printf ("    cookies={\n");
            if (self->cookies) {
                size_t size = zframe_size (self->cookies);
                byte *data = zframe_data (self->cookies);
                printf ("        size=%td\n", zframe_size (self->cookies));
                if (size > 32)
                    size = 32;
                int cookies_index;
                for (cookies_index = 0; cookies_index < size; cookies_index++) {
                    if (cookies_index && (cookies_index % 4 == 0))
                        printf ("-");
                    printf ("%02X", data [cookies_index]);
                }
            }
            printf ("    }\n");
            break;
            
        case ZRE_MSG_JOIN:
            puts ("JOIN:");
            if (self->group)
                printf ("    group='%s'\n", self->group);
            else
                printf ("    group=\n");
            printf ("    status=%ld\n", (long) self->status);
            break;
            
        case ZRE_MSG_LEAVE:
            puts ("LEAVE:");
            if (self->group)
                printf ("    group='%s'\n", self->group);
            else
                printf ("    group=\n");
            printf ("    status=%ld\n", (long) self->status);
            break;
            
        case ZRE_MSG_PING:
            puts ("PING:");
            break;
            
        case ZRE_MSG_PING_OK:
            puts ("PING_OK:");
            break;
            
    }
}


//  --------------------------------------------------------------------------
//  Get/set the message address

zframe_t *
zre_msg_address (zre_msg_t *self)
{
    assert (self);
    return self->address;
}

void
zre_msg_address_set (zre_msg_t *self, zframe_t *address)
{
    if (self->address)
        zframe_destroy (&self->address);
    self->address = zframe_dup (address);
}


//  --------------------------------------------------------------------------
//  Get/set the zre_msg id

int
zre_msg_id (zre_msg_t *self)
{
    assert (self);
    return self->id;
}

void
zre_msg_id_set (zre_msg_t *self, int id)
{
    self->id = id;
}

//  --------------------------------------------------------------------------
//  Return a printable command string

char *
zre_msg_command (zre_msg_t *self)
{
    assert (self);
    switch (self->id) {
        case ZRE_MSG_HELLO:
            return ("HELLO");
            break;
        case ZRE_MSG_WHISPER:
            return ("WHISPER");
            break;
        case ZRE_MSG_SHOUT:
            return ("SHOUT");
            break;
        case ZRE_MSG_JOIN:
            return ("JOIN");
            break;
        case ZRE_MSG_LEAVE:
            return ("LEAVE");
            break;
        case ZRE_MSG_PING:
            return ("PING");
            break;
        case ZRE_MSG_PING_OK:
            return ("PING_OK");
            break;
    }
    return "?";
}

//  --------------------------------------------------------------------------
//  Get the zre_msg sequence

uint32_t
zre_msg_sequence (zre_msg_t *self)
{
    assert (self);
    return self->sequence;
}

//  --------------------------------------------------------------------------
//  Get/set the from field

char *
zre_msg_from (zre_msg_t *self)
{
    assert (self);
    return self->from;
}

void
zre_msg_from_set (zre_msg_t *self, char *format, ...)
{
    //  Format into newly allocated string
    assert (self);
    va_list argptr;
    va_start (argptr, format);
    free (self->from);
    self->from = (char *) malloc (STRING_MAX + 1);
    assert (self->from);
    vsnprintf (self->from, STRING_MAX, format, argptr);
    va_end (argptr);
}


//  --------------------------------------------------------------------------
//  Get/set the port field

byte
zre_msg_port (zre_msg_t *self)
{
    assert (self);
    return self->port;
}

void
zre_msg_port_set (zre_msg_t *self, byte port)
{
    assert (self);
    self->port = port;
}


//  --------------------------------------------------------------------------
//  Get/set the groups field

zlist_t *
zre_msg_groups (zre_msg_t *self)
{
    assert (self);
    return self->groups;
}

//  Greedy function, takes ownership of name; if you don't want that
//  then use zlist_dup() to pass a copy of groups

void
zre_msg_groups_set (zre_msg_t *self, zlist_t *groups)
{
    assert (self);
    zlist_destroy (&self->groups);
    self->groups = groups;
}

//  --------------------------------------------------------------------------
//  Iterate through the groups field, and append a groups value

char *
zre_msg_groups_first (zre_msg_t *self)
{
    assert (self);
    if (self->groups)
        return (char *) (zlist_first (self->groups));
    else
        return NULL;
}

char *
zre_msg_groups_next (zre_msg_t *self)
{
    assert (self);
    if (self->groups)
        return (char *) (zlist_next (self->groups));
    else
        return NULL;
}

void
zre_msg_groups_append (zre_msg_t *self, char *format, ...)
{
    //  Format into newly allocated string
    assert (self);
    va_list argptr;
    va_start (argptr, format);
    char *string = (char *) malloc (STRING_MAX + 1);
    assert (string);
    vsnprintf (string, STRING_MAX, format, argptr);
    va_end (argptr);
    
    //  Attach string to list
    if (!self->groups) {
        self->groups = zlist_new ();
        zlist_autofree (self->groups);
    }
    zlist_append (self->groups, string);
}

size_t
zre_msg_groups_size (zre_msg_t *self)
{
    return zlist_size (self->groups);
}


//  --------------------------------------------------------------------------
//  Get/set the status field

byte
zre_msg_status (zre_msg_t *self)
{
    assert (self);
    return self->status;
}

void
zre_msg_status_set (zre_msg_t *self, byte status)
{
    assert (self);
    self->status = status;
}


//  --------------------------------------------------------------------------
//  Get/set the headers field

zhash_t *
zre_msg_headers (zre_msg_t *self)
{
    assert (self);
    return self->headers;
}

//  Greedy function, takes ownership of name; if you don't want that
//  then use zhash_dup() to pass a copy of headers

void
zre_msg_headers_set (zre_msg_t *self, zhash_t *headers)
{
    assert (self);
    zhash_destroy (&self->headers);
    self->headers = headers;
}

//  --------------------------------------------------------------------------
//  Get/set a value in the headers dictionary

char *
zre_msg_headers_string (zre_msg_t *self, char *key, char *default_value)
{
    assert (self);
    char *value = NULL;
    if (self->headers)
        value = (char *) (zhash_lookup (self->headers, key));
    if (!value)
        value = default_value;

    return value;
}

int64_t
zre_msg_headers_number (zre_msg_t *self, char *key, int64_t default_value)
{
    assert (self);
    int64_t value = default_value;
    char *string;
    if (self->headers)
        string = (char *) (zhash_lookup (self->headers, key));
    if (string)
        value = atol (string);

    return value;
}

void
zre_msg_headers_insert (zre_msg_t *self, char *key, char *format, ...)
{
    //  Format string into buffer
    assert (self);
    va_list argptr;
    va_start (argptr, format);
    char *string = (char *) malloc (STRING_MAX + 1);
    assert (string);
    vsnprintf (string, STRING_MAX, format, argptr);
    va_end (argptr);

    //  Store string in hash table
    if (!self->headers)
        self->headers = zhash_new ();
    if (zhash_insert (self->headers, key, string) == 0)
        zhash_freefn (self->headers, key, free);
}

size_t
zre_msg_headers_size (zre_msg_t *self)
{
    return zhash_size (self->headers);
}


//  --------------------------------------------------------------------------
//  Get/set the cookies field

zframe_t *
zre_msg_cookies (zre_msg_t *self)
{
    assert (self);
    return self->cookies;
}

//  Takes ownership of supplied frame
void
zre_msg_cookies_set (zre_msg_t *self, zframe_t *frame)
{
    assert (self);
    if (self->cookies)
        zframe_destroy (&self->cookies);
    self->cookies = frame;
}

//  --------------------------------------------------------------------------
//  Get/set the group field

char *
zre_msg_group (zre_msg_t *self)
{
    assert (self);
    return self->group;
}

void
zre_msg_group_set (zre_msg_t *self, char *format, ...)
{
    //  Format into newly allocated string
    assert (self);
    va_list argptr;
    va_start (argptr, format);
    free (self->group);
    self->group = (char *) malloc (STRING_MAX + 1);
    assert (self->group);
    vsnprintf (self->group, STRING_MAX, format, argptr);
    va_end (argptr);
}



//  --------------------------------------------------------------------------
//  Selftest

int
zre_msg_test (bool verbose)
{
    printf (" * zre_msg: ");

    //  Simple create/destroy test
    zre_msg_t *self = zre_msg_new (0);
    assert (self);
    zre_msg_destroy (&self);

    //  Create pair of sockets we can send through
    zctx_t *ctx = zctx_new ();
    assert (ctx);

    void *output = zsocket_new (ctx, ZMQ_DEALER);
    assert (output);
    zsocket_bind (output, "inproc://selftest");
    void *input = zsocket_new (ctx, ZMQ_ROUTER);
    assert (input);
    zsocket_connect (input, "inproc://selftest");
    
    //  Encode/send/decode and verify each message type

    self = zre_msg_new (ZRE_MSG_HELLO);
    zre_msg_from_set (self, "Life is short but Now lasts for ever");
    zre_msg_port_set (self, 123);
    zre_msg_groups_append (self, "Name: %s", "Brutus");
    zre_msg_groups_append (self, "Age: %d", 43);
    zre_msg_status_set (self, 123);
    zre_msg_headers_insert (self, "Name", "Brutus");
    zre_msg_headers_insert (self, "Age", "%d", 43);
    zre_msg_send (&self, output, 1);
    
    self = zre_msg_recv (input);
    assert (self);
    assert (streq (zre_msg_from (self), "Life is short but Now lasts for ever"));
    assert (zre_msg_port (self) == 123);
    assert (zre_msg_groups_size (self) == 2);
    assert (streq (zre_msg_groups_first (self), "Name: Brutus"));
    assert (streq (zre_msg_groups_next (self), "Age: 43"));
    assert (zre_msg_status (self) == 123);
    assert (zre_msg_headers_size (self) == 2);
    assert (streq (zre_msg_headers_string (self, "Name", "?"), "Brutus"));
    assert (zre_msg_headers_number (self, "Age", 0) == 43);
    zre_msg_destroy (&self);

    self = zre_msg_new (ZRE_MSG_WHISPER);
    zre_msg_cookies_set (self, zframe_new ("Captcha Diem", 12));
    zre_msg_send (&self, output, 1);
    
    self = zre_msg_recv (input);
    assert (self);
    assert (zframe_streq (zre_msg_cookies (self), "Captcha Diem"));
    zre_msg_destroy (&self);

    self = zre_msg_new (ZRE_MSG_SHOUT);
    zre_msg_group_set (self, "Life is short but Now lasts for ever");
    zre_msg_cookies_set (self, zframe_new ("Captcha Diem", 12));
    zre_msg_send (&self, output, 1);
    
    self = zre_msg_recv (input);
    assert (self);
    assert (streq (zre_msg_group (self), "Life is short but Now lasts for ever"));
    assert (zframe_streq (zre_msg_cookies (self), "Captcha Diem"));
    zre_msg_destroy (&self);

    self = zre_msg_new (ZRE_MSG_JOIN);
    zre_msg_group_set (self, "Life is short but Now lasts for ever");
    zre_msg_status_set (self, 123);
    zre_msg_send (&self, output, 1);
    
    self = zre_msg_recv (input);
    assert (self);
    assert (streq (zre_msg_group (self), "Life is short but Now lasts for ever"));
    assert (zre_msg_status (self) == 123);
    zre_msg_destroy (&self);

    self = zre_msg_new (ZRE_MSG_LEAVE);
    zre_msg_group_set (self, "Life is short but Now lasts for ever");
    zre_msg_status_set (self, 123);
    zre_msg_send (&self, output, 1);
    
    self = zre_msg_recv (input);
    assert (self);
    assert (streq (zre_msg_group (self), "Life is short but Now lasts for ever"));
    assert (zre_msg_status (self) == 123);
    zre_msg_destroy (&self);

    self = zre_msg_new (ZRE_MSG_PING);
    zre_msg_send (&self, output, 1);
    
    self = zre_msg_recv (input);
    assert (self);
    zre_msg_destroy (&self);

    self = zre_msg_new (ZRE_MSG_PING_OK);
    zre_msg_send (&self, output, 1);
    
    self = zre_msg_recv (input);
    assert (self);
    zre_msg_destroy (&self);

    zctx_destroy (&ctx);
    printf ("OK\n");
    return 0;
}