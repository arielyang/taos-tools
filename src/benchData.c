/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the MIT license as published by the Free Software
 * Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "benchData.h"
#include "bench.h"

const char charset[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";

const char* locations[] = {"San Francisco", "Los Angles", "San Diego",
                           "San Jose", "Palo Alto", "Campbell", "Mountain View",
                           "Sunnyvale", "Santa Clara", "Cupertino"};

const char* locations_sml[] = {"San\\ Francisco", "Los\\ Angles", "San\\ Diego",
                           "San\\ Jose", "Palo\\ Alto", "Campbell", "Mountain\\ View",
                           "Sunnyvale", "Santa\\ Clara", "Cupertino"};

const char* locations_chinese[] = {"旧金山","洛杉矶","圣地亚哥","圣何塞","帕洛阿尔托","坎贝尔",
                                   "山景城","森尼韦尔","圣克拉拉","库比蒂诺"};

static int usc2utf8(char *p, int unic) {
    if (unic <= 0x0000007F) {
        *p = (unic & 0x7F);
        return 1;
    } else if (unic >= 0x00000080 && unic <= 0x000007FF) {
        *(p + 1) = (unic & 0x3F) | 0x80;
        *p = ((unic >> 6) & 0x1F) | 0xC0;
        return 2;
    } else if (unic >= 0x00000800 && unic <= 0x0000FFFF) {
        *(p + 2) = (unic & 0x3F) | 0x80;
        *(p + 1) = ((unic >> 6) & 0x3F) | 0x80;
        *p = ((unic >> 12) & 0x0F) | 0xE0;
        return 3;
    } else if (unic >= 0x00010000 && unic <= 0x001FFFFF) {
        *(p + 3) = (unic & 0x3F) | 0x80;
        *(p + 2) = ((unic >> 6) & 0x3F) | 0x80;
        *(p + 1) = ((unic >> 12) & 0x3F) | 0x80;
        *p = ((unic >> 18) & 0x07) | 0xF0;
        return 4;
    } else if (unic >= 0x00200000 && unic <= 0x03FFFFFF) {
        *(p + 4) = (unic & 0x3F) | 0x80;
        *(p + 3) = ((unic >> 6) & 0x3F) | 0x80;
        *(p + 2) = ((unic >> 12) & 0x3F) | 0x80;
        *(p + 1) = ((unic >> 18) & 0x3F) | 0x80;
        *p = ((unic >> 24) & 0x03) | 0xF8;
        return 5;
    } else if (unic >= 0x04000000) {
        *(p + 5) = (unic & 0x3F) | 0x80;
        *(p + 4) = ((unic >> 6) & 0x3F) | 0x80;
        *(p + 3) = ((unic >> 12) & 0x3F) | 0x80;
        *(p + 2) = ((unic >> 18) & 0x3F) | 0x80;
        *(p + 1) = ((unic >> 24) & 0x3F) | 0x80;
        *p = ((unic >> 30) & 0x01) | 0xFC;
        return 6;
    }
    return 0;
}

static void rand_string(char *str, int size, bool chinese) {
    if (chinese) {
        char *pstr = str;
        int   move = 0;
        while (size > 0) {
            // Chinese Character need 3 bytes space
            if (size < 3) {
                break;
            }
            // Basic Chinese Character's Unicode is from 0x4e00 to 0x9fa5
            int unic = 0x4e00 + taosRandom() % (0x9fa5 - 0x4e00);
            move = usc2utf8(pstr, unic);
            pstr += move;
            size -= move;
        }
    } else {
        str[0] = 0;
        if (size > 0) {
            //--size;
            int n;
            for (n = 0; n < size; n++) {
                int key = abs(taosRandom()) % (int)(sizeof(charset) - 1);
                str[n] = charset[key];
            }
            str[n] = 0;
        }
    }
}

int stmt_prepare(SSuperTable *stbInfo, TAOS_STMT *stmt, uint64_t tableSeq) {
    int   len = 0;
    char *prepare = benchCalloc(1, BUFFER_SIZE, true);
    if (stbInfo->autoCreateTable) {
        len += sprintf(prepare + len,
                       "INSERT INTO ? USING `%s` TAGS (%s) VALUES(?",
                       stbInfo->stbName,
                       stbInfo->tagDataBuf + stbInfo->lenOfTags * tableSeq);
    } else {
        len += sprintf(prepare + len, "INSERT INTO ? VALUES(?");
    }

    BArray* cols = stbInfo->columns;
    for (int col = 0; col < cols->size; col++) {
        len += sprintf(prepare + len, ",?");
    }
    sprintf(prepare + len, ")");
    if (g_arguments->prepared_rand < g_arguments->reqPerReq) {
        infoPrint(stdout,
                  "in stmt mode, batch size(%"PRId64") can not be larger than prepared "
                  "sample data size(%" PRId64
                  "), restart with larger prepared_rand or batch size will be "
                  "auto set to %" PRId64 "\n",
                  g_arguments->reqPerReq, g_arguments->prepared_rand,
                  g_arguments->prepared_rand);
        g_arguments->reqPerReq = g_arguments->prepared_rand;
    }
    if (taos_stmt_prepare(stmt, prepare, strlen(prepare))) {
        errorPrint(stderr, "taos_stmt_prepare(%s) failed\n", prepare);
        tmfree(prepare);
        return -1;
    }
    tmfree(prepare);
    return 0;
}

static int generateSampleFromCsvForStb(char *buffer, char *file, int32_t length,
                                       int64_t size) {
    size_t  n = 0;
    ssize_t readLen = 0;
    char *  line = NULL;
    int     getRows = 0;

    FILE *fp = fopen(file, "r");
    if (fp == NULL) {
        errorPrint(stderr, "Failed to open sample file: %s, reason:%s\n", file,
                   strerror(errno));
        return -1;
    }
    while (1) {
#if defined(WIN32) || defined(WIN64)
        readLen = tgetline(&line, &n, fp);
#else
        readLen = getline(&line, &n, fp);
#endif
        if (-1 == readLen) {
            if (0 != fseek(fp, 0, SEEK_SET)) {
                errorPrint(stderr, "Failed to fseek file: %s, reason:%s\n",
                           file, strerror(errno));
                fclose(fp);
                return -1;
            }
            continue;
        }

        if (('\r' == line[readLen - 1]) || ('\n' == line[readLen - 1])) {
            line[--readLen] = 0;
        }

        if (readLen == 0) {
            continue;
        }

        if (readLen > length) {
            infoPrint(
                stdout,
                "sample row len[%d] overflow define schema len[%d], so discard "
                "this row\n",
                (int32_t)readLen, length);
            continue;
        }

        memcpy(buffer + getRows * length, line, readLen);
        getRows++;

        if (getRows == size) {
            break;
        }
    }

    fclose(fp);
    tmfree(line);
    return 0;
}

static int getAndSetRowsFromCsvFile(SSuperTable *stbInfo) {
    int32_t code = -1;
    FILE *  fp = fopen(stbInfo->sampleFile, "r");
    int     line_count = 0;
    char *  buf = NULL;
    if (fp == NULL) {
        errorPrint(stderr, "Failed to open sample file: %s, reason:%s\n",
                   stbInfo->sampleFile, strerror(errno));
        goto free_of_get_set_rows_from_csv;
    }
    buf = benchCalloc(1, TSDB_MAX_SQL_LEN, false);
    while (fgets(buf, TSDB_MAX_SQL_LEN, fp)) {
        line_count++;
    }
    stbInfo->insertRows = line_count;
    code = 0;
free_of_get_set_rows_from_csv:
    fclose(fp);
    tmfree(buf);
    return code;
}

static uint32_t calcRowLen(SSuperTable *stbInfo, BArray* fields) {
    uint32_t ret = 0;
    for (int i = 0; i < fields->size; i++) {
        Field * field = benchArrayGet(fields, i);
        switch (field->type) {
            case TSDB_DATA_TYPE_BINARY:
            case TSDB_DATA_TYPE_NCHAR:
                ret += field->length + 3;
                break;

            case TSDB_DATA_TYPE_INT:
            case TSDB_DATA_TYPE_UINT:
                ret += INT_BUFF_LEN;
                break;

            case TSDB_DATA_TYPE_BIGINT:
            case TSDB_DATA_TYPE_UBIGINT:
                ret += BIGINT_BUFF_LEN;
                break;

            case TSDB_DATA_TYPE_SMALLINT:
            case TSDB_DATA_TYPE_USMALLINT:
                ret += SMALLINT_BUFF_LEN;
                break;

            case TSDB_DATA_TYPE_TINYINT:
            case TSDB_DATA_TYPE_UTINYINT:
                ret += TINYINT_BUFF_LEN;
                break;

            case TSDB_DATA_TYPE_BOOL:
                ret += BOOL_BUFF_LEN;
                break;

            case TSDB_DATA_TYPE_FLOAT:
                ret += FLOAT_BUFF_LEN;
                break;

            case TSDB_DATA_TYPE_DOUBLE:
                ret += DOUBLE_BUFF_LEN;
                break;

            case TSDB_DATA_TYPE_TIMESTAMP:
                ret += TIMESTAMP_BUFF_LEN;
                break;
            case TSDB_DATA_TYPE_JSON:
                ret += field->length * fields->size;
                break;
        }
        ret += 1;
        if (stbInfo->iface == SML_IFACE || stbInfo->iface == SML_REST_IFACE) {
            ret += SML_LINE_SQL_SYNTAX_OFFSET + strlen(field->name);
            ret += 2 * TSDB_TABLE_NAME_LEN * 2 + SML_LINE_SQL_SYNTAX_OFFSET;
        }
    }
    ret += TIMESTAMP_BUFF_LEN;
    return ret;
}

void generateRandData(SSuperTable *stbInfo, char *sampleDataBuf,
                      int lenOfOneRow, BArray *fields, int64_t loop,
                      bool isTag) {
    int     iface = stbInfo->iface;
    int     line_protocol = stbInfo->lineProtocol;
    int64_t pos = 0;
    if (iface == STMT_IFACE) {
        for (int i = 0; i < fields->size; ++i) {
            Field* field = benchArrayGet(fields, i);
            if (field->type == TSDB_DATA_TYPE_BINARY ||
                field->type == TSDB_DATA_TYPE_NCHAR) {
                field->data = benchCalloc(1, loop * (field->length + 1), true);
            } else {
                field->data = benchCalloc(1, loop * field->length, true);
            }
        }
    }
    for (int64_t k = 0; k < loop; ++k) {
        pos = k * lenOfOneRow;
        if (line_protocol == TSDB_SML_LINE_PROTOCOL &&
            (iface == SML_IFACE || iface == SML_REST_IFACE) && isTag) {
            pos += sprintf(sampleDataBuf + pos, "%s,", stbInfo->stbName);
        }
        for (int i = 0; i < fields->size; ++i) {
            Field* field = benchArrayGet(fields, i);
            if (iface == TAOSC_IFACE || iface == REST_IFACE) {
                if (field->null) {
                    continue;
                }
                if (field->length == 0) {
                    pos += sprintf(sampleDataBuf + pos, "null,");
                    continue;
                }
            }
            switch (field->type) {
                case TSDB_DATA_TYPE_BOOL: {
                    bool rand_bool = (taosRandom() % 2) & 1;
                    if (iface == STMT_IFACE) {
                        ((bool *)field->data)[k] = rand_bool;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%s,",
                                       field->name,
                                       rand_bool ? "true" : "false");
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (isTag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%s ",
                                           field->name,
                                           rand_bool ? "true" : "false");
                        } else {
                            pos += sprintf(sampleDataBuf + pos, "%s ",
                                           rand_bool ? "true" : "false");
                        }
                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%s,",
                                       rand_bool ? "true" : "false");
                    }
                    break;
                }
                case TSDB_DATA_TYPE_TINYINT: {
                    int8_t tinyint =
                        field->min +
                        (taosRandom() % (field->max - field->min));
                    if (iface == STMT_IFACE) {
                        ((int8_t *)field->data)[k] = tinyint;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%di8,",
                                       field->name, tinyint);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (isTag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%di8 ",
                                           field->name, tinyint);
                        } else {
                            pos +=
                                sprintf(sampleDataBuf + pos, "%di8 ", tinyint);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%d,", tinyint);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_UTINYINT: {
                    uint8_t utinyint =
                        field->min +
                        (taosRandom() % (field->max - field->min));
                    if (iface == STMT_IFACE) {
                        ((uint8_t *)field->data)[k] = utinyint;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%uu8,",
                                       field->name, utinyint);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (isTag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%uu8 ",
                                           field->name, utinyint);
                        } else {
                            pos +=
                                sprintf(sampleDataBuf + pos, "%uu8 ", utinyint);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%u,", utinyint);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_SMALLINT: {
                    int16_t smallint =
                        field->min +
                        (taosRandom() % (field->max - field->min));
                    if (iface == STMT_IFACE) {
                        ((int16_t *)field->data)[k] = smallint;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%di16,",
                                       field->name, smallint);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (isTag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%di16 ",
                                           field->name, smallint);
                        } else {
                            pos += sprintf(sampleDataBuf + pos, "%di16 ",
                                           smallint);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%d,", smallint);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_USMALLINT: {
                    uint16_t usmallint =
                        field->min +
                        (taosRandom() % (field->max - field->min));
                    if (iface == STMT_IFACE) {
                        ((uint16_t *)field->data)[k] = usmallint;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%uu16,",
                                       field->name, usmallint);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (isTag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%uu16 ",
                                           field->name, usmallint);
                        } else {
                            pos += sprintf(sampleDataBuf + pos, "%uu16 ",
                                           usmallint);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%u,", usmallint);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_INT: {
                    int32_t int_ =
                            field->min +
                            (taosRandom() % (field->max - field->min));

                    if (iface == STMT_IFACE) {
                        ((int32_t *)field->data)[k] = int_;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%di32,",
                                       field->name, int_);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (isTag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%di32 ",
                                           field->name, int_);
                        } else {
                            pos += sprintf(sampleDataBuf + pos, "%di32 ", int_);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%d,", int_);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_BIGINT: {
                    int32_t int_ = field->min +
                           (taosRandom() % (field->max - field->min));
                    if (iface == STMT_IFACE) {
                        ((int64_t *)field->data)[k] = int_;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%di64,",
                                       field->name, int_);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (isTag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%di64 ",
                                           field->name, int_);
                        } else {
                            pos += sprintf(sampleDataBuf + pos, "%di64 ", int_);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%d,", int_);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_UINT: {
                    uint32_t uint =
                        field->min +
                        (taosRandom() % (field->max - field->min));
                    if (iface == STMT_IFACE) {
                        ((uint32_t *)field->data)[k] = uint;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%uu32,",
                                       field->name, uint);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (isTag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%uu32 ",
                                           field->name, uint);
                        } else {
                            pos += sprintf(sampleDataBuf + pos, "%uu32 ", uint);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%u,", uint);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_UBIGINT:
                case TSDB_DATA_TYPE_TIMESTAMP: {
                    uint32_t ubigint =
                        field->min +
                        (taosRandom() % (field->max - field->min));
                    if (iface == STMT_IFACE) {
                        ((uint64_t *)field->data)[k] = ubigint;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        (line_protocol == TSDB_SML_LINE_PROTOCOL ||
                         (isTag && line_protocol == TSDB_SML_TELNET_PROTOCOL))) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%uu64,",
                                       field->name, ubigint);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (isTag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%uu64 ",
                                           field->name, ubigint);
                        } else {
                            pos +=
                                sprintf(sampleDataBuf + pos, "%uu64 ", ubigint);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%u,", ubigint);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_FLOAT: {
                    float float_ = (float)(field->min +
                                           (taosRandom() %
                                            (field->max - field->min)) +
                                           (taosRandom() % 1000) / 1000.0);
                    /*if (g_arguments->demo_mode && i == 0) {
                        float_ = (float)(9.8 + 0.04 * (taosRandom() % 10) +
                                         float_ / 1000000000);
                    } else if (g_arguments->demo_mode && i == 2) {
                        float_ = (float)((115 + taosRandom() % 10 +
                                          float_ / 1000000000) /
                                         360);
                    }*/
                    if (iface == STMT_IFACE) {
                        ((float *)(field->data))[k] = float_;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%ff32,",
                                       field->name, float_);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (isTag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%ff32 ",
                                           field->name, float_);
                        } else {
                            pos +=
                                sprintf(sampleDataBuf + pos, "%ff32 ", float_);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%f,", float_);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_DOUBLE: {
                    double double_ =
                        (double)(field->min +
                                 (taosRandom() %
                                  (field->max - field->min)) +
                                 taosRandom() % 1000000 / 1000000.0);
                    if (iface == STMT_IFACE) {
                        ((double *)field->data)[k] = double_;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%ff64,",
                                       field->name, double_);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (isTag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%ff64 ",
                                           field->name, double_);
                        } else {
                            pos +=
                                sprintf(sampleDataBuf + pos, "%ff64 ", double_);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%f,", double_);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_BINARY:
                case TSDB_DATA_TYPE_NCHAR: {
                    char *tmp = benchCalloc(1, field->length + 1, false);
                    if (g_arguments->demo_mode) {
                        if (g_arguments->chinese) {
                            sprintf(tmp, "%s", locations_chinese[taosRandom() % 10]);
                        } else if (stbInfo->iface == SML_IFACE) {
                            sprintf(tmp, "%s", locations_sml[taosRandom() % 10]);
                        } else {
                            sprintf(tmp, "%s", locations[taosRandom() % 10]);
                        }
                    } else if (field->values) {
                        cJSON *buf = cJSON_GetArrayItem(
                            field->values,
                            taosRandom() %
                                cJSON_GetArraySize(field->values));
                        sprintf(tmp, "%s", buf->valuestring);
                    } else {
                        rand_string(tmp, field->length,
                                    g_arguments->chinese);
                    }
                    if (iface == STMT_IFACE) {
                        sprintf((char *)field->data + k * field->length,
                                "%s", tmp);
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        field->type == TSDB_DATA_TYPE_BINARY &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=\"%s\",",
                                       field->name, tmp);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               field->type == TSDB_DATA_TYPE_NCHAR &&
                               line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=L\"%s\",",
                                       field->name, tmp);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               field->type == TSDB_DATA_TYPE_BINARY &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (isTag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=L\"%s\" ",
                                           field->name, tmp);
                        } else {
                            pos += sprintf(sampleDataBuf + pos, "\"%s\" ", tmp);
                        }

                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               field->type == TSDB_DATA_TYPE_NCHAR &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (isTag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=L\"%s\" ",
                                           field->name, tmp);
                        } else {
                            pos +=
                                sprintf(sampleDataBuf + pos, "L\"%s\" ", tmp);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "'%s',", tmp);
                    }
                    tmfree(tmp);
                    break;
                }
                case TSDB_DATA_TYPE_JSON: {
                    pos += sprintf(sampleDataBuf + pos, "'{");
                    for (int j = 0; j < stbInfo->tags->size; ++j) {
                        Field * tag = benchArrayGet(stbInfo->tags, j);
                        pos += sprintf(sampleDataBuf + pos, "\"k%d\":", j);
                        char *buf = benchCalloc(1, tag->length + 1, false);
                        rand_string(buf, tag->length,
                                    g_arguments->chinese);
                        pos += sprintf(sampleDataBuf + pos, "\"%s\",", buf);
                        tmfree(buf);
                    }
                    pos += sprintf(sampleDataBuf + pos - 1, "}'");
                    goto skip;
                }
            }
        }
    skip:
        *(sampleDataBuf + pos - 1) = 0;
    }
}

int prepare_sample_data(SDataBase *  database, SSuperTable *stbInfo) {
    stbInfo->lenOfTags = calcRowLen(stbInfo, stbInfo->tags);
    stbInfo->lenOfCols = calcRowLen(stbInfo, stbInfo->columns);
    BArray * columns = stbInfo->columns;
    if (stbInfo->partialColumnNum != 0 &&
        (stbInfo->iface == TAOSC_IFACE || stbInfo->iface == REST_IFACE)) {
        if (stbInfo->partialColumnNum > columns->size) {
            stbInfo->partialColumnNum = columns->size;
        } else {
            stbInfo->partialColumnNameBuf = benchCalloc(1, BUFFER_SIZE, true);
            int pos = 0;
            pos += sprintf(stbInfo->partialColumnNameBuf + pos, "ts");
            for (int i = 0; i < stbInfo->partialColumnNum; ++i) {
                Field * column = benchArrayGet(columns, i);
                pos += sprintf(stbInfo->partialColumnNameBuf + pos, ",%s",
                               column->name);
            }
            for (int i = stbInfo->partialColumnNum; i < columns->size;
                 ++i) {
                Field * column = benchArrayGet(columns, i);
                column->null = true;
            }
            debugPrint(stdout, "partialColumnNameBuf: %s\n",
                       stbInfo->partialColumnNameBuf);
        }
    } else {
        stbInfo->partialColumnNum = columns->size;
    }
    stbInfo->sampleDataBuf =
        benchCalloc(1, stbInfo->lenOfCols * g_arguments->prepared_rand, true);
    infoPrint(stdout,
              "generate stable<%s> columns data with lenOfCols<%u> * "
              "prepared_rand<%" PRIu64 ">\n",
              stbInfo->stbName, stbInfo->lenOfCols, g_arguments->prepared_rand);
    if (stbInfo->random_data_source) {
        generateRandData(stbInfo, stbInfo->sampleDataBuf, stbInfo->lenOfCols,
                         stbInfo->columns, g_arguments->prepared_rand, false);
    } else {
        if (stbInfo->useSampleTs) {
            if (getAndSetRowsFromCsvFile(stbInfo)) return -1;
        }
        if (generateSampleFromCsvForStb(stbInfo->sampleDataBuf,
                                        stbInfo->sampleFile, stbInfo->lenOfCols,
                                        g_arguments->prepared_rand)) {
            return -1;
        }
    }
    debugPrint(stdout, "sampleDataBuf: %s\n", stbInfo->sampleDataBuf);

    BArray * tags = stbInfo->tags;
    if (!stbInfo->childTblExists && tags->size != 0) {
        stbInfo->tagDataBuf =
            benchCalloc(1, stbInfo->childTblCount * stbInfo->lenOfTags, true);
        infoPrint(stdout,
                  "generate stable<%s> tags data with lenOfTags<%u> * "
                  "childTblCount<%" PRIu64 ">\n",
                  stbInfo->stbName, stbInfo->lenOfTags, stbInfo->childTblCount);
        if (stbInfo->tagsFile[0] != 0) {
            if (generateSampleFromCsvForStb(
                    stbInfo->tagDataBuf, stbInfo->tagsFile, stbInfo->lenOfTags,
                    stbInfo->childTblCount)) {
                return -1;
            }
        } else {
            generateRandData(stbInfo, stbInfo->tagDataBuf, stbInfo->lenOfTags,
                             stbInfo->tags, stbInfo->childTblCount, true);
        }
        debugPrint(stdout, "tagDataBuf: %s\n", stbInfo->tagDataBuf);
    }

    if (stbInfo->iface == REST_IFACE || stbInfo->iface == SML_REST_IFACE) {
        if (stbInfo->tcpTransfer && stbInfo->iface == SML_REST_IFACE) {
            if (convertHostToServAddr(g_arguments->host,
                                      g_arguments->telnet_tcp_port,
                                      &(g_arguments->serv_addr))) {
                errorPrint(stderr, "%s\n", "convert host to server address");
                return -1;
            }
        } else {
            if (convertHostToServAddr(g_arguments->host,
                                      g_arguments->port + TSDB_PORT_HTTP,
                                      &(g_arguments->serv_addr))) {
                errorPrint(stderr, "%s\n", "convert host to server address");
                return -1;
            }
        }
    }
    return 0;
}

int64_t getTSRandTail(int64_t timeStampStep, int32_t seq, int disorderRatio,
                      int disorderRange) {
    int64_t randTail = timeStampStep * seq;
    if (disorderRatio > 0) {
        int rand_num = taosRandom() % 100;
        if (rand_num < disorderRatio) {
            randTail = (randTail + (taosRandom() % disorderRange + 1)) * (-1);
        }
    }
    return randTail;
}

int bindParamBatch(threadInfo *pThreadInfo, uint32_t batch, int64_t startTime) {
    TAOS_STMT *  stmt = pThreadInfo->stmt;
    SSuperTable *stbInfo = pThreadInfo->stbInfo;
    BArray * columns = stbInfo->columns;
    memset(pThreadInfo->bindParams, 0,
           (sizeof(TAOS_MULTI_BIND) * (columns->size + 1)));
    memset(pThreadInfo->is_null, 0, batch);

    for (int c = 0; c < columns->size + 1; c++) {
        TAOS_MULTI_BIND *param =
            (TAOS_MULTI_BIND *)(pThreadInfo->bindParams +
                                sizeof(TAOS_MULTI_BIND) * c);
        char data_type;
        if (c == 0) {
            data_type = TSDB_DATA_TYPE_TIMESTAMP;
            param->buffer_length = sizeof(int64_t);
            param->buffer = pThreadInfo->bind_ts_array;
        } else {
            Field * column = benchArrayGet(columns, c - 1);
            data_type = column->type;
            param->buffer = column->data;
            param->buffer_length = column->length;
            debugPrint(stdout, "col[%d]: type: %s, len: %d\n", c,
                       taos_convert_datatype_to_string(data_type),
                       column->length);
        }
        param->buffer_type = data_type;
        param->length = benchCalloc(batch, sizeof(int32_t), true);

        for (int b = 0; b < batch; b++) {
            param->length[b] = (int32_t)param->buffer_length;
        }
        param->is_null = pThreadInfo->is_null;
        param->num = batch;
    }

    for (uint32_t k = 0; k < batch; k++) {
        /* columnCount + 1 (ts) */
        if (stbInfo->disorderRatio) {
            *(pThreadInfo->bind_ts_array + k) =
                startTime + getTSRandTail(stbInfo->timestamp_step, k,
                                          stbInfo->disorderRatio,
                                          stbInfo->disorderRange);
        } else {
            *(pThreadInfo->bind_ts_array + k) =
                startTime + stbInfo->timestamp_step * k;
        }
    }

    if (taos_stmt_bind_param_batch(
            stmt, (TAOS_MULTI_BIND *)pThreadInfo->bindParams)) {
        errorPrint(stderr, "taos_stmt_bind_param_batch() failed! reason: %s\n",
                   taos_stmt_errstr(stmt));
        return -1;
    }

    for (int c = 0; c < stbInfo->columns->size + 1; c++) {
        TAOS_MULTI_BIND *param =
            (TAOS_MULTI_BIND *)(pThreadInfo->bindParams +
                                sizeof(TAOS_MULTI_BIND) * c);
        tmfree(param->length);
    }

    // if msg > 3MB, break
    if (taos_stmt_add_batch(stmt)) {
        errorPrint(stderr, "taos_stmt_add_batch() failed! reason: %s\n",
                   taos_stmt_errstr(stmt));
        return -1;
    }
    return batch;
}

int32_t generateSmlJsonTags(cJSON *tagsList, SSuperTable *stbInfo,
                            uint64_t start_table_from, int tbSeq) {
    int32_t code = -1;
    BArray *fields = stbInfo->tags;
    cJSON * tags = cJSON_CreateObject();
    char *  tbName = benchCalloc(1, TSDB_TABLE_NAME_LEN + BIGINT_BUFF_LEN, false);
    snprintf(tbName, TSDB_TABLE_NAME_LEN + BIGINT_BUFF_LEN, "%s%" PRIu64 "",
             stbInfo->childTblPrefix, tbSeq + start_table_from);
    cJSON_AddStringToObject(tags, "id", tbName);
    char *tagName = benchCalloc(1, TSDB_MAX_TAGS, false);
    for (int i = 0; i < fields->size; i++) {
        Field * field = benchArrayGet(fields, i);
        cJSON *tag = cJSON_CreateObject();
        snprintf(tagName, TSDB_MAX_TAGS, "t%d", i);
        switch (field->type) {
            case TSDB_DATA_TYPE_BOOL: {
                cJSON_AddBoolToObject(tag, "value", (taosRandom() % 2) & 1);
                cJSON_AddStringToObject(tag, "type", "bool");
                break;
            }

            case TSDB_DATA_TYPE_TINYINT: {
                cJSON_AddNumberToObject(
                    tag, "value",
                    field->min +
                        (taosRandom() % (field->max - field->min)));
                cJSON_AddStringToObject(tag, "type", "tinyint");
                break;
            }

            case TSDB_DATA_TYPE_SMALLINT: {
                cJSON_AddNumberToObject(
                    tag, "value",
                    field->min +
                        (taosRandom() % (field->max - field->min)));
                cJSON_AddStringToObject(tag, "type", "smallint");
                break;
            }

            case TSDB_DATA_TYPE_INT: {
                cJSON_AddNumberToObject(
                    tag, "value",
                    field->min +
                        (taosRandom() % (field->max - field->min)));
                cJSON_AddStringToObject(tag, "type", "int");
                break;
            }

            case TSDB_DATA_TYPE_BIGINT: {
                cJSON_AddNumberToObject(
                    tag, "value",
                    (double)field->min +
                        (taosRandom() % (field->max - field->min)));
                cJSON_AddStringToObject(tag, "type", "bigint");
                break;
            }

            case TSDB_DATA_TYPE_FLOAT: {
                cJSON_AddNumberToObject(
                    tag, "value",
                    (float)(field->min +
                            (taosRandom() % (field->max - field->min)) +
                            taosRandom() % 1000 / 1000.0));
                cJSON_AddStringToObject(tag, "type", "float");
                break;
            }

            case TSDB_DATA_TYPE_DOUBLE: {
                cJSON_AddNumberToObject(
                    tag, "value",
                    (double)(field->min +
                             (taosRandom() %
                              (field->max - field->min)) +
                             taosRandom() % 1000000 / 1000000.0));
                cJSON_AddStringToObject(tag, "type", "double");
                break;
            }

            case TSDB_DATA_TYPE_BINARY:
            case TSDB_DATA_TYPE_NCHAR: {
                char *buf = (char *)benchCalloc(field->length + 1, 1, false);
                rand_string(buf, field->length, g_arguments->chinese);
                if (field->type == TSDB_DATA_TYPE_BINARY) {
                    cJSON_AddStringToObject(tag, "value", buf);
                    cJSON_AddStringToObject(tag, "type", "binary");
                } else {
                    cJSON_AddStringToObject(tag, "value", buf);
                    cJSON_AddStringToObject(tag, "type", "nchar");
                }
                tmfree(buf);
                break;
            }
            default:
                errorPrint(
                    stderr,
                    "unknown data type (%d) for schemaless json protocol\n",
                    field->type);
                goto free_of_generate_sml_json_tag;
        }
        cJSON_AddItemToObject(tags, tagName, tag);
    }
    cJSON_AddItemToArray(tagsList, tags);
    code = 0;
free_of_generate_sml_json_tag:
    tmfree(tagName);
    tmfree(tbName);
    return code;
}

int32_t generateSmlJsonCols(cJSON *array, cJSON *tag, SSuperTable *stbInfo,
                            uint32_t time_precision, int64_t timestamp) {
    cJSON * record = cJSON_CreateObject();
    cJSON * ts = cJSON_CreateObject();
    BArray *columns = stbInfo->columns;
    cJSON_AddNumberToObject(ts, "value", (double)timestamp);
    if (time_precision == TSDB_SML_TIMESTAMP_MILLI_SECONDS) {
        cJSON_AddStringToObject(ts, "type", "ms");
    } else if (time_precision == TSDB_SML_TIMESTAMP_MICRO_SECONDS) {
        cJSON_AddStringToObject(ts, "type", "us");
    } else if (time_precision == TSDB_SML_TIMESTAMP_NANO_SECONDS) {
        cJSON_AddStringToObject(ts, "type", "ns");
    } else {
        errorPrint(stderr, "Unknown time precision %d\n", time_precision);
        return -1;
    }
    cJSON *value = cJSON_CreateObject();
    Field * column = benchArrayGet(columns, 0);
    switch (column->type) {
        case TSDB_DATA_TYPE_BOOL:
            cJSON_AddBoolToObject(value, "value", (taosRandom() % 2) & 1);
            cJSON_AddStringToObject(value, "type", "bool");
            break;
        case TSDB_DATA_TYPE_TINYINT:
            cJSON_AddNumberToObject(
                value, "value",
                column->min +
                    (taosRandom() % (column->max - column->min)));
            cJSON_AddStringToObject(value, "type", "tinyint");
            break;
        case TSDB_DATA_TYPE_SMALLINT:
            cJSON_AddNumberToObject(
                value, "value",
                column->min +
                    (taosRandom() % (column->max - column->min)));
            cJSON_AddStringToObject(value, "type", "smallint");
            break;
        case TSDB_DATA_TYPE_INT:
            cJSON_AddNumberToObject(
                value, "value",
                column->min +
                    (taosRandom() % (column->max - column->min)));
            cJSON_AddStringToObject(value, "type", "int");
            break;
        case TSDB_DATA_TYPE_BIGINT:
            cJSON_AddNumberToObject(
                value, "value",
                (double)column->min +
                    (taosRandom() % (column->max - column->min)));
            cJSON_AddStringToObject(value, "type", "bigint");
            break;
        case TSDB_DATA_TYPE_FLOAT:
            cJSON_AddNumberToObject(
                value, "value",
                (float)(column->min +
                        (taosRandom() % (column->max - column->min)) +
                        taosRandom() % 1000 / 1000.0));
            cJSON_AddStringToObject(value, "type", "float");
            break;
        case TSDB_DATA_TYPE_DOUBLE:
            cJSON_AddNumberToObject(
                value, "value",
                (double)(column->min +
                         (taosRandom() % (column->max - column->min)) +
                         taosRandom() % 1000000 / 1000000.0));
            cJSON_AddStringToObject(value, "type", "double");
            break;
        case TSDB_DATA_TYPE_BINARY:
        case TSDB_DATA_TYPE_NCHAR: {
            char *buf = (char *)benchCalloc(column->length + 1, 1, false);
            rand_string(buf, column->length, g_arguments->chinese);
            if (column->type == TSDB_DATA_TYPE_BINARY) {
                cJSON_AddStringToObject(value, "value", buf);
                cJSON_AddStringToObject(value, "type", "binary");
            } else {
                cJSON_AddStringToObject(value, "value", buf);
                cJSON_AddStringToObject(value, "type", "nchar");
            }
            tmfree(buf);
            break;
        }
        default:
            errorPrint(stderr,
                       "unknown data type (%d) for schemaless json protocol\n",
                       column->type);
            return -1;
    }
    cJSON_AddItemToObject(record, "timestamp", ts);
    cJSON_AddItemToObject(record, "value", value);
    cJSON_AddItemToObject(record, "tags", tag);
    cJSON_AddStringToObject(record, "metric", stbInfo->stbName);
    cJSON_AddItemToArray(array, record);
    return 0;
}
