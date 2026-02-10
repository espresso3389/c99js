/*
 * jsmn test for c99js
 *
 * c99js doesn't support goto, so we inline a patched version of jsmn
 * that replaces "goto found" in jsmn_parse_primitive with a flag.
 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>

/* === Begin inlined & patched jsmn === */

typedef enum {
  JSMN_UNDEFINED = 0,
  JSMN_OBJECT = 1,
  JSMN_ARRAY = 2,
  JSMN_STRING = 4,
  JSMN_PRIMITIVE = 8
} jsmntype_t;

enum jsmnerr {
  JSMN_ERROR_NOMEM = -1,
  JSMN_ERROR_INVAL = -2,
  JSMN_ERROR_PART = -3
};

typedef struct jsmntok {
  jsmntype_t type;
  int start;
  int end;
  int size;
} jsmntok_t;

typedef struct jsmn_parser {
  unsigned int pos;
  unsigned int toknext;
  int toksuper;
} jsmn_parser;

static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser, jsmntok_t *tokens,
                                    const size_t num_tokens) {
  jsmntok_t *tok;
  if (parser->toknext >= num_tokens) {
    return NULL;
  }
  tok = &tokens[parser->toknext];
  parser->toknext = parser->toknext + 1;
  tok->start = -1;
  tok->end = -1;
  tok->size = 0;
  return tok;
}

static void jsmn_fill_token(jsmntok_t *token, const jsmntype_t type,
                             const int start, const int end) {
  token->type = type;
  token->start = start;
  token->end = end;
  token->size = 0;
}

/*
 * Patched: replaced "goto found" with a "found" flag and early break.
 */
static int jsmn_parse_primitive(jsmn_parser *parser, const char *js,
                                 const size_t len, jsmntok_t *tokens,
                                 const size_t num_tokens) {
  jsmntok_t *token;
  int start;
  int found_flag;

  start = parser->pos;
  found_flag = 0;

  for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
    switch (js[parser->pos]) {
    case ':':
    case '\t':
    case '\r':
    case '\n':
    case ' ':
    case ',':
    case ']':
    case '}':
      found_flag = 1;
      break;
    default:
      break;
    }
    if (found_flag) {
      break;
    }
    if (js[parser->pos] < 32 || js[parser->pos] >= 127) {
      parser->pos = start;
      return JSMN_ERROR_INVAL;
    }
  }

  /* "found" label equivalent */
  if (tokens == NULL) {
    parser->pos--;
    return 0;
  }
  token = jsmn_alloc_token(parser, tokens, num_tokens);
  if (token == NULL) {
    parser->pos = start;
    return JSMN_ERROR_NOMEM;
  }
  jsmn_fill_token(token, JSMN_PRIMITIVE, start, parser->pos);
  parser->pos--;
  return 0;
}

static int jsmn_parse_string(jsmn_parser *parser, const char *js,
                              const size_t len, jsmntok_t *tokens,
                              const size_t num_tokens) {
  jsmntok_t *token;
  int start;
  int i;
  char c;

  start = parser->pos;
  parser->pos++;

  for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
    c = js[parser->pos];

    if (c == '\"') {
      if (tokens == NULL) {
        return 0;
      }
      token = jsmn_alloc_token(parser, tokens, num_tokens);
      if (token == NULL) {
        parser->pos = start;
        return JSMN_ERROR_NOMEM;
      }
      jsmn_fill_token(token, JSMN_STRING, start + 1, parser->pos);
      return 0;
    }

    if (c == '\\' && parser->pos + 1 < len) {
      parser->pos++;
      switch (js[parser->pos]) {
      case '\"':
      case '/':
      case '\\':
      case 'b':
      case 'f':
      case 'r':
      case 'n':
      case 't':
        break;
      case 'u':
        parser->pos++;
        for (i = 0; i < 4 && parser->pos < len && js[parser->pos] != '\0'; i++) {
          if (!((js[parser->pos] >= 48 && js[parser->pos] <= 57) ||
                (js[parser->pos] >= 65 && js[parser->pos] <= 70) ||
                (js[parser->pos] >= 97 && js[parser->pos] <= 102))) {
            parser->pos = start;
            return JSMN_ERROR_INVAL;
          }
          parser->pos++;
        }
        parser->pos--;
        break;
      default:
        parser->pos = start;
        return JSMN_ERROR_INVAL;
      }
    }
  }
  parser->pos = start;
  return JSMN_ERROR_PART;
}

static int jsmn_parse(jsmn_parser *parser, const char *js, const size_t len,
                       jsmntok_t *tokens, const unsigned int num_tokens) {
  int r;
  int i;
  jsmntok_t *token;
  int count;
  char c;
  jsmntype_t type;
  jsmntok_t *t;

  count = parser->toknext;

  for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
    c = js[parser->pos];
    switch (c) {
    case '{':
    case '[':
      count++;
      if (tokens == NULL) {
        break;
      }
      token = jsmn_alloc_token(parser, tokens, num_tokens);
      if (token == NULL) {
        return JSMN_ERROR_NOMEM;
      }
      if (parser->toksuper != -1) {
        t = &tokens[parser->toksuper];
        t->size++;
      }
      if (c == '{') {
        token->type = JSMN_OBJECT;
      } else {
        token->type = JSMN_ARRAY;
      }
      token->start = parser->pos;
      parser->toksuper = parser->toknext - 1;
      break;
    case '}':
    case ']':
      if (tokens == NULL) {
        break;
      }
      if (c == '}') {
        type = JSMN_OBJECT;
      } else {
        type = JSMN_ARRAY;
      }
      for (i = parser->toknext - 1; i >= 0; i--) {
        token = &tokens[i];
        if (token->start != -1 && token->end == -1) {
          if (token->type != type) {
            return JSMN_ERROR_INVAL;
          }
          parser->toksuper = -1;
          token->end = parser->pos + 1;
          break;
        }
      }
      if (i == -1) {
        return JSMN_ERROR_INVAL;
      }
      for (; i >= 0; i--) {
        token = &tokens[i];
        if (token->start != -1 && token->end == -1) {
          parser->toksuper = i;
          break;
        }
      }
      break;
    case '\"':
      r = jsmn_parse_string(parser, js, len, tokens, num_tokens);
      if (r < 0) {
        return r;
      }
      count++;
      if (parser->toksuper != -1 && tokens != NULL) {
        tokens[parser->toksuper].size++;
      }
      break;
    case '\t':
    case '\r':
    case '\n':
    case ' ':
      break;
    case ':':
      parser->toksuper = parser->toknext - 1;
      break;
    case ',':
      if (tokens != NULL && parser->toksuper != -1 &&
          tokens[parser->toksuper].type != JSMN_ARRAY &&
          tokens[parser->toksuper].type != JSMN_OBJECT) {
        for (i = parser->toknext - 1; i >= 0; i--) {
          if (tokens[i].type == JSMN_ARRAY || tokens[i].type == JSMN_OBJECT) {
            if (tokens[i].start != -1 && tokens[i].end == -1) {
              parser->toksuper = i;
              break;
            }
          }
        }
      }
      break;
    default:
      r = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
      if (r < 0) {
        return r;
      }
      count++;
      if (parser->toksuper != -1 && tokens != NULL) {
        tokens[parser->toksuper].size++;
      }
      break;
    }
  }

  if (tokens != NULL) {
    for (i = parser->toknext - 1; i >= 0; i--) {
      if (tokens[i].start != -1 && tokens[i].end == -1) {
        return JSMN_ERROR_PART;
      }
    }
  }

  return count;
}

static void jsmn_init(jsmn_parser *parser) {
  parser->pos = 0;
  parser->toknext = 0;
  parser->toksuper = -1;
}

/* === End inlined & patched jsmn === */

static const char *type_name(jsmntype_t t) {
    switch (t) {
        case JSMN_OBJECT:    return "OBJECT";
        case JSMN_ARRAY:     return "ARRAY";
        case JSMN_STRING:    return "STRING";
        case JSMN_PRIMITIVE: return "PRIMITIVE";
        default:             return "UNDEFINED";
    }
}

int main(void) {
    const char *json = "{\"name\":\"Alice\",\"age\":30,\"active\":true,\"items\":[1,2,3]}";
    jsmn_parser parser;
    jsmntok_t tokens[64];
    int r;
    int i;
    int pass;

    pass = 1;

    printf("=== jsmn JSON parser test via c99js ===\n");
    printf("Input: %s\n\n", json);

    jsmn_init(&parser);
    r = jsmn_parse(&parser, json, strlen(json), tokens, 64);

    if (r < 0) {
        printf("FAIL: jsmn_parse returned error %d\n", r);
        return 1;
    }

    printf("Token count: %d\n\n", r);

    /* Print all tokens */
    printf("%-4s  %-10s  %-5s  %-5s  %s\n", "Idx", "Type", "Start", "End", "Value");
    printf("----  ----------  -----  -----  -----\n");
    for (i = 0; i < r; i++) {
        int len;
        len = tokens[i].end - tokens[i].start;
        printf("%-4d  %-10s  %-5d  %-5d  %.*s\n",
               i,
               type_name(tokens[i].type),
               tokens[i].start,
               tokens[i].end,
               len, json + tokens[i].start);
    }

    printf("\n");

    /* Verify token count: expect 12 */
    if (r != 12) {
        printf("FAIL: expected 12 tokens, got %d\n", r);
        pass = 0;
    }

    /* Verify root is an object */
    if (tokens[0].type != JSMN_OBJECT) {
        printf("FAIL: token 0 should be OBJECT\n");
        pass = 0;
    }

    /* Verify key "name" */
    if (tokens[1].type != JSMN_STRING ||
        strncmp(json + tokens[1].start, "name", 4) != 0) {
        printf("FAIL: token 1 should be STRING 'name'\n");
        pass = 0;
    }

    /* Verify value "Alice" */
    if (tokens[2].type != JSMN_STRING ||
        strncmp(json + tokens[2].start, "Alice", 5) != 0) {
        printf("FAIL: token 2 should be STRING 'Alice'\n");
        pass = 0;
    }

    /* Verify key "age" */
    if (tokens[3].type != JSMN_STRING ||
        strncmp(json + tokens[3].start, "age", 3) != 0) {
        printf("FAIL: token 3 should be STRING 'age'\n");
        pass = 0;
    }

    /* Verify value 30 */
    if (tokens[4].type != JSMN_PRIMITIVE ||
        strncmp(json + tokens[4].start, "30", 2) != 0) {
        printf("FAIL: token 4 should be PRIMITIVE '30'\n");
        pass = 0;
    }

    /* Verify key "active" */
    if (tokens[5].type != JSMN_STRING ||
        strncmp(json + tokens[5].start, "active", 6) != 0) {
        printf("FAIL: token 5 should be STRING 'active'\n");
        pass = 0;
    }

    /* Verify value true */
    if (tokens[6].type != JSMN_PRIMITIVE ||
        strncmp(json + tokens[6].start, "true", 4) != 0) {
        printf("FAIL: token 6 should be PRIMITIVE 'true'\n");
        pass = 0;
    }

    /* Verify key "items" */
    if (tokens[7].type != JSMN_STRING ||
        strncmp(json + tokens[7].start, "items", 5) != 0) {
        printf("FAIL: token 7 should be STRING 'items'\n");
        pass = 0;
    }

    /* Verify array */
    if (tokens[8].type != JSMN_ARRAY) {
        printf("FAIL: token 8 should be ARRAY\n");
        pass = 0;
    }

    /* Verify array elements 1, 2, 3 */
    if (tokens[9].type != JSMN_PRIMITIVE ||
        strncmp(json + tokens[9].start, "1", 1) != 0) {
        printf("FAIL: token 9 should be PRIMITIVE '1'\n");
        pass = 0;
    }
    if (tokens[10].type != JSMN_PRIMITIVE ||
        strncmp(json + tokens[10].start, "2", 1) != 0) {
        printf("FAIL: token 10 should be PRIMITIVE '2'\n");
        pass = 0;
    }
    if (tokens[11].type != JSMN_PRIMITIVE ||
        strncmp(json + tokens[11].start, "3", 1) != 0) {
        printf("FAIL: token 11 should be PRIMITIVE '3'\n");
        pass = 0;
    }

    /* Verify object has 4 keys */
    if (tokens[0].size != 4) {
        printf("FAIL: root object should have size 4, got %d\n", tokens[0].size);
        pass = 0;
    }

    /* Verify array has 3 elements */
    if (tokens[8].size != 3) {
        printf("FAIL: array should have size 3, got %d\n", tokens[8].size);
        pass = 0;
    }

    if (pass) {
        printf("PASS: All jsmn token verifications succeeded!\n");
    } else {
        printf("FAIL: Some verifications failed.\n");
    }

    return pass ? 0 : 1;
}
