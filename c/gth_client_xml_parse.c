//----------------------------------------------------------------------
// Scanner and parser for the responses generated by a Corelatus GTH.
//
// The scanner and parser are both translated from a hand-coded parser
// in a functional language. It's easier to understand them if you look
// at the source (gth_xml_scan.erl).
//
// API documentation is available at www.corelatus.com/gth/api/
//
// Author: Matt Lang (matthias@corelatus.se)
//
// Copyright (c) 2009, Corelatus AB Stockholm
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of Corelatus nor the
//       names of its contributors may be used to endorse or promote products
//       derived from this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY Corelatus ''AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL Corelatus BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 
//----------------------------------------------------------------------

// 
// $Id: gth_client_xml_parse.c,v 1.12 2010-06-14 13:20:49 matthias Exp $
//
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <malloc.h>

#include "gth_client_xml_parse.h"

//======================================================================
// First, the scanner. (the parser is after the next line rule)

enum Token_type 
  {
    TOK_END        = '.',
    TOK_WHITESPACE = 'w',
    TOK_STRING     = 's',
    TOK_OPEN       = '<',
    TOK_CLOSE      = '>',
    TOK_EQUAL      = '=',
    TOK_SLASH      = '/',
    TOK_NAME       = 'n',
    TOK_TEXT       = 't'
  };

typedef struct 
{
  enum Token_type type;
  char* payload;
} GTH_token;

// We're inside a string. Make a token out of the rest of the string.
//
// Return: the number of characters that were in the string.
static int scan_string(const char* string, GTH_token *token, const char end)
{
  char* endptr;
  int len;

  endptr = strchr(string, end);
  assert(endptr);

  len = endptr - string;
  token->type = TOK_STRING;
  token->payload = malloc(len + 1);
  assert(token->payload);

  strncpy(token->payload, string, len);
  token->payload[len] = 0;

  return len;  
}

// We're inside a name. Make a token out of it.
//
// Return: the number of characters that were in the name.
static int scan_name(const char* string, GTH_token *token)
{
  int len;

  len = strcspn(string, " =\r\n/><");

  token->type = TOK_NAME;
  token->payload = malloc(len + 1);
  assert(token->payload);

  strncpy(token->payload, string, len);
  token->payload[len] = 0;

  return len;  
}

// Turn the given string into an array of tokens, terminated by TOK_END
//
// The caller is responsible for calling gth_free_tokens() on the
// token array.
//
// Returns zero on success
//
int gth_scan(const char *string, GTH_token **ret_tokens) 
{
  size_t len;
  int max_token = 0;
  int tokens_used = 0;
  GTH_token *start = 0;
  GTH_token *current = 0;

  //----
 outside_tag:
  tokens_used = (current - start);
  if ( tokens_used == max_token) {
    max_token = max_token * 2 + 5;  // 0, 5, 15, 35, 75
    start = realloc(start, max_token * sizeof(GTH_token));
    current = start + tokens_used;
  }

  if (*string == 0) goto end_of_input;

  if (*string == '<') {
    current->type = TOK_OPEN;
    current->payload = 0;
    goto inside_tag;
  }

  len = strcspn(string, "<");

  // Make a text token, but only if the text is non-whitespace
  if (strspn(string, "\r\n\t ") < len) {
    current->type = TOK_TEXT;
    current->payload = malloc(len + 1);
    strncpy(current->payload, string, len);
    current->payload[len] = 0;
    current++;
  }
  string += len;

  goto outside_tag;

  //----

 inside_tag:
  string++;
  current++;
  tokens_used = (current - start);
  if ( tokens_used == max_token) {
    max_token = max_token * 2 + 5;  // 0, 5, 15, 35, 75
    start = realloc(start, max_token * sizeof(GTH_token));
    current = start + tokens_used;
  }

 skip_whitespace:
  if (*string == 0) goto end_of_input;

  if (*string == ' ' || *string == '\n' || *string == '\r' || *string == '\t') {
    string++;
    goto skip_whitespace;
  }

  if (*string == '"' || *string == '\'') {
      len = scan_string(string+1, current, *string);
      string += len + 1; // closing quote will be skipped at top of loop
      goto inside_tag;
    }

  if (*string == '>') {
    current->type = TOK_CLOSE;
    current->payload = 0;
    string++;
    current++;
    goto outside_tag;
  }

  if (*string == '=') {
    current->type = TOK_EQUAL;
    current->payload = 0;
    goto inside_tag;
  }

  if (*string == '/') {
    current->type = TOK_SLASH;
    current->payload = 0;
    goto inside_tag;
  }

  len = scan_name(string, current);
  string += len - 1;
  goto inside_tag;

 end_of_input:
  current->type = TOK_END;
  current->payload = 0;

  *ret_tokens = start;

  return 0;
}

// Free a sequence of tokens.
void gth_free_tokens(GTH_token *token) 
{
  GTH_token *current = token;

  while (current->type != TOK_END) {
    switch (current->type) {
    case TOK_NAME: case TOK_STRING: case TOK_TEXT:
      free(current->payload);
      break;

    default:
      assert(current->payload == 0);
      break;
    }
    current++;
  }

  free(token);
}

// Constructor
static void new_resp(GTH_resp *resp, const GTH_resp_type type) 
{
  resp->type = type;

  resp->attributes = 0;
  resp->n_attributes = 0;
  resp->allocated_attributes = 0;

  resp->children = 0;
  resp->allocated_children = 0;
  resp->n_children = 0;

  resp->text = 0;
}

static void free_resp(GTH_resp *resp);

// Free a tree of resps. Only the top-level resp was allocated with
// malloc, so only the top-level one is freed directly. The children were
// all allocated en-bloc with realloc.
void gth_free_resp(GTH_resp *resp) {
  if (resp == 0) {
    fprintf(stderr, "warning: ignoring attempt to free a null resp\n");
    return;
  }

  free_resp(resp);
  free(resp);
}

static void free_resp(GTH_resp *resp) 
{
  int x;

  // Free the attributes and their contents
  for (x = 0; x < resp->n_attributes; x++)
    {
      free(resp->attributes[x].key);
      free(resp->attributes[x].value);
    }
  free(resp->attributes);
  resp->attributes = 0;

  // Recursively free all children
  for (x = 0; x < resp->n_children; x++)
    {
      free_resp(resp->children + x);
    }
  free(resp->children);
  resp->children = 0;

  if (resp->text) free(resp->text);
}


// Add another child to a resp. Return a pointer to that child.
static GTH_resp *resp_add_child(GTH_resp *resp, GTH_resp_type type)
{
  GTH_resp *newborn;

  if (resp->n_children >= resp->allocated_children) 
    {
      resp->allocated_children = resp->allocated_children * 2 + 3;
      resp->children = realloc(resp->children, 
			       resp->allocated_children 
			       * sizeof(GTH_resp));
    }

  newborn = resp->children + resp->n_children;
  resp->n_children++;

  new_resp(newborn, type);
  return newborn;  
}

// Add another attribute to a resp. The key and value are copied shallow,
// i.e. the caller may not free() them.
static void resp_add_attribute(GTH_resp *resp, char *key, char *value)
{
  if (resp->allocated_attributes <= resp->n_attributes)
    {
      resp->allocated_attributes = 2*resp->allocated_attributes + 2;
      resp->attributes = realloc(resp->attributes, 
				 resp->allocated_attributes * 
				 sizeof(GTH_attribute));
    }

  resp->attributes[resp->n_attributes].key = key;
  resp->attributes[resp->n_attributes].value = value;
  resp->n_attributes++;
}


// These functions all return either a GTH_resp, or null on parse error.
static GTH_token *parse(GTH_token *token, GTH_resp *resp);
static GTH_token *attributes(GTH_token *token, GTH_resp *resp);
static GTH_token *parse_inside_tag(GTH_token *token, char **text);
static enum Token_type name_to_token_type(const char* name);

void gth_free_resp(GTH_resp *resp);
static int can_lookahead(const GTH_token *token, int n);

void print_tokens(const GTH_token *token);
void print_attributes(const GTH_resp *resp);

// Scan a string. Return a pointer-to-resp tree. Caller must free the tree
// when done, using gth_free_resp()
GTH_resp *gth_parse(const char *string) 
{
  GTH_token *tokens;
  GTH_token *end;
  GTH_resp *resp = malloc(sizeof(GTH_resp));
  GTH_resp *old;

  new_resp(resp, GTH_RESP_ERROR);
  
  gth_scan(string, &tokens);
  end = parse(tokens, resp);

  assert(end->type == TOK_END);
  gth_free_tokens(tokens);

  // After parsing's done, turn the forest into a tree
  assert(resp->n_children == 1);
  assert(resp->n_attributes == 0);
  old = resp->children;
  *resp = resp->children[0];
  free(old);

  return resp;  
}

// Parse a sequence of tokens made by gth_scan().
//
// Return: 0 or a GTH_resp. The caller is responsible for 
// calling gth_free_resp() on the returned GTH_resp.
static GTH_token *parse(GTH_token *token, GTH_resp *list_of_trees)
{
  GTH_token *open;
  GTH_token *name;
  GTH_token *slash;
  GTH_token *close;
  char *tag_name;
  GTH_resp *tree = 0;

  if (!can_lookahead(token, 2)) {
    return token;
  }
  open = token++;
  name = token++;

  if (open->type == TOK_OPEN && name->type == TOK_SLASH) { // func. clause
    return (token - 2);
  } else if (open->type == TOK_OPEN && name->type == TOK_NAME) { // func. clause
    tag_name = name->payload;
    tree = resp_add_child(list_of_trees, name_to_token_type(tag_name));

    token = attributes(token, tree);

    if (token->type == TOK_CLOSE) {  // Second case clause 
      char *text;
      token++;
      token = parse_inside_tag(token, &text);
      tree->text = text;

      token = parse(token, tree);

      if (can_lookahead(token, 4)) {
	open = token++;
	slash = token++;
	name = token++;
	close = token++;

	if (open->type     != TOK_OPEN 
	    || slash->type != TOK_SLASH 
	    || name->type  != TOK_NAME 
	    || strcmp(name->payload, tag_name) != 0
	    || close->type != TOK_CLOSE) assert(!"parse ");

	tree->text = text;
	
	return token;
      }
    }
    else {                         // first case clause 
      if (can_lookahead(token, 2)) {
	GTH_token *slash = token++;
	GTH_token *close = token++;
	
	assert(slash->type == TOK_SLASH);
	assert(close->type == TOK_CLOSE);

	token = parse(token, list_of_trees);
	return token;
      }
    }
  }
    
  return token;
}
  
GTH_token *parse_inside_tag(GTH_token *token, char **text) {

  if (token->type == TOK_TEXT) {
    assert(token->payload);
    *text = token->payload;
    token->payload = 0;
    token++;
  } 
  else {
    *text = 0;
  }
  return token;
}

static GTH_token *attributes(GTH_token *token, GTH_resp *resp) {
  GTH_token *name;
  GTH_token *equal;
  GTH_token *string;

  if (token->type == TOK_CLOSE) return token;
  if (token->type == TOK_SLASH) {
    assert((token+1)->type == TOK_CLOSE);
    return token;
  }
  if (!can_lookahead(token, 3)) return token;

  name = token++;
  equal = token++;
  string = token++;

  assert(name->type == TOK_NAME);
  assert(equal->type == TOK_EQUAL);
  assert(string->type == TOK_STRING);

  resp_add_attribute(resp, name->payload, string->payload);

  // Zero the string pointers in the tokens; the parse tree now owns them
  name->payload = 0;  
  string->payload = 0;

  return attributes(token, resp);
}


static enum Token_type name_to_token_type(const char* name)
{
  if (!strcmp(name, "alarm"))           return  GTH_RESP_ALARM;
  if (!strcmp(name, "alert"))           return  GTH_RESP_ALERT;
  if (!strcmp(name, "atm_message"))     return  GTH_RESP_ATM_MESSAGE;
  if (!strcmp(name, "attribute"))       return  GTH_RESP_ATTRIBUTE;
  if (!strcmp(name, "controller"))      return  GTH_RESP_CONTROLLER;
  if (!strcmp(name, "ebs"))             return  GTH_RESP_EBS;
  if (!strcmp(name, "error"))           return  GTH_RESP_ERROR;
  if (!strcmp(name, "event"))           return  GTH_RESP_EVENT;
  if (!strcmp(name, "fatality"))        return  GTH_RESP_FATALITY;
  if (!strcmp(name, "fault"))           return  GTH_RESP_FAULT;
  if (!strcmp(name, "f_relay_message")) return  GTH_RESP_F_RELAY_MESSAGE;
  if (!strcmp(name, "info"))            return  GTH_RESP_INFO;
  if (!strcmp(name, "job"))             return  GTH_RESP_JOB;
  if (!strcmp(name, "l1_message"))      return  GTH_RESP_L1_MESSAGE;
  if (!strcmp(name, "l2_alarm"))        return  GTH_RESP_L2_ALARM;
  if (!strcmp(name, "l2_socket_alert")) return  GTH_RESP_L2_SOCKET_ALERT;
  if (!strcmp(name, "lapd_message"))    return  GTH_RESP_LAPD_MESSAGE;
  if (!strcmp(name, "level"))           return  GTH_RESP_LEVEL;
  if (!strcmp(name, "message_ended"))   return  GTH_RESP_MESSAGE_ENDED;
  if (!strcmp(name, "mtp2_message"))    return  GTH_RESP_MTP2_MESSAGE;
  if (!strcmp(name, "ok"))              return  GTH_RESP_OK;
  if (!strcmp(name, "resource"))        return  GTH_RESP_RESOURCE;
  if (!strcmp(name, "slip"))            return  GTH_RESP_SLIP;
  if (!strcmp(name, "sync_message"))    return  GTH_RESP_SYNC_MESSAGE;
  if (!strcmp(name, "tone"))            return  GTH_RESP_TONE;
  if (!strcmp(name, "state"))           return  GTH_RESP_STATE;

  fprintf(stderr, "don't know what this is: %s\n", name);

  assert(!"unknown tag");
  return 0; // never reached
}

// Are there at least N tokens left to look at.
//
// Return 1 if there are, 0 otherwise.
static int can_lookahead(const GTH_token *token, int n) {
  const GTH_token* current = token;

  while (n > 0) 
    {
      if (current->type == TOK_END) return 0;
      current++;
      n--;
    }
  return 1;
}   
//----------------------------------------------------------------------

// for debugging
void print_tokens(const GTH_token *token) 
{
  while (token->type != TOK_END) {
    switch (token->type) {

    case TOK_WHITESPACE: printf("-"); break;

    case TOK_STRING: printf("*%s*", token->payload); break;

    case TOK_OPEN:  printf("<"); break;
    case TOK_CLOSE: printf(">"); break;
    case TOK_EQUAL: printf("="); break;
    case TOK_SLASH: printf("/"); break;
    case TOK_NAME:  printf("(%s)", token->payload); break;
    case TOK_TEXT:  printf("[%s]", token->payload); break;
    default:
      assert(!"this cannot happen");
    }
    token++;
  }
  printf(".\n");
}

void print_children(GTH_resp *resp) {
  int x;

  for (x = 0; x < resp->n_children; x++)
    {
      gth_print_tree(resp->children + x);
    }
}


// For debugging.
//
void gth_print_tree(GTH_resp *resp) {
  assert(resp);

  switch (resp->type) {
  case GTH_RESP_ALARM:
    fprintf(stderr, "alarm: ");
    print_attributes(resp);
    break;

  case GTH_RESP_ALERT:
    fprintf(stderr, "alert: ");
    print_attributes(resp);
    break;

  case GTH_RESP_ATM_MESSAGE:
    fprintf(stderr, "atm_message: ");
    print_attributes(resp);
    break;

  case GTH_RESP_ATTRIBUTE: 
    fprintf(stderr, "    attribute: ");
    print_attributes(resp);
    break;

  case GTH_RESP_CONTROLLER: 
    fprintf(stderr, "    controller: ");
    print_attributes(resp);
    break;

  case GTH_RESP_EBS:
    fprintf(stderr, "ebs message printing NYI\n");
    break;

  case GTH_RESP_ERROR: 
    fprintf(stderr, "error: ");
    print_attributes(resp);
    if (resp->text) fprintf(stderr, "[%s]", resp->text);
    fprintf(stderr, "\n");
    break;

  case GTH_RESP_EVENT: 
    fprintf(stderr, "event: "); 
    print_children(resp);
    fprintf(stderr, "\n");
    break;

  case GTH_RESP_FATALITY: 
    fprintf(stderr, "fatality: "); 
    print_attributes(resp);
    break;

  case GTH_RESP_FAULT: 
    fprintf(stderr, "fault: "); 
    print_attributes(resp);
    break;

  case GTH_RESP_F_RELAY_MESSAGE: 
    fprintf(stderr, "  f_relay_message: "); 
    print_attributes(resp);
    break;

  case GTH_RESP_INFO: 
    fprintf(stderr, "  info: "); 
    print_attributes(resp);
    break;

  case GTH_RESP_JOB: 
    fprintf(stderr, "job, %d attrs, id=%s\n", resp->n_attributes, resp->attributes[0].value); 
    break;

  case GTH_RESP_L1_MESSAGE: 
    fprintf(stderr, "  l1 message ");
    print_attributes(resp);
    break;

  case GTH_RESP_L2_ALARM: 
    fprintf(stderr, "  l2_alarm: "); 
    print_attributes(resp);
    break;

  case GTH_RESP_L2_SOCKET_ALERT: 
    fprintf(stderr, "  l2_socket_alert: "); 
    print_attributes(resp);
    break;

  case GTH_RESP_LAPD_MESSAGE: 
    fprintf(stderr, "  lapd_message: "); 
    print_attributes(resp);
    break;

  case GTH_RESP_LEVEL: 
    fprintf(stderr, "  level: "); 
    print_attributes(resp);
    break;

  case GTH_RESP_MESSAGE_ENDED: 
    fprintf(stderr, "  message ended:"); 
    print_attributes(resp);
    break;

  case GTH_RESP_MTP2_MESSAGE: 
    fprintf(stderr, "  mtp2_message: "); 
    print_attributes(resp);
    break;

  case GTH_RESP_OK: 
    fprintf(stderr, "ok\n"); 
    break;

  case GTH_RESP_RESOURCE: 
    fprintf(stderr, "  resource: %s", resp->attributes[0].value);
    print_children(resp);
    fprintf(stderr, "\n"); 
    break;

  case GTH_RESP_SLIP: 
    fprintf(stderr, "  slip: "); 
    print_attributes(resp);
    break;

  case GTH_RESP_STATE: 
    fprintf(stderr, "state: ");
    print_children(resp);
    fprintf(stderr, "\n");
    break;

  case GTH_RESP_SYNC_MESSAGE:
    fprintf(stderr, "  sync message "); 
    print_attributes(resp);
    break;

  case GTH_RESP_TONE: 
    fprintf(stderr, "  tone: "); 
    print_attributes(resp);
    break;

  default:
    fprintf(stderr, "default, type=%d\n", resp->type);
    assert(!"impossible");
  }
}

void print_attributes(const GTH_resp *resp) {
  int x;

  for (x = 0; x < resp->n_attributes; x++)
    {
      fprintf(stderr, "%s=%s ", 
	      resp->attributes[x].key, resp->attributes[x].value);
    }

  fprintf(stderr, "\n");
}
