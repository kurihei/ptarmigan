/*
 *  Copyright (C) 2017, Nayuta, Inc. All Rights Reserved
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing,
 *  software distributed under the License is distributed on an
 *  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *  KIND, either express or implied.  See the License for the
 *  specific language governing permissions and limitations
 *  under the License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ucoind.h"
#include "conf.h"
#include "misc.h"


#define M_OPTIONS_INIT  (0xff)
#define M_OPTIONS_CONN  (0xf0)
#define M_OPTIONS_EXEC  (2)
#define M_OPTIONS_STOP  (1)
#define M_OPTIONS_HELP  (0)
#define BUFFER_SIZE     (256 * 1024)

#define M_NEXT              ","
#define M_QQ(str)           "\"" str "\""
#define M_STR(item,value)   M_QQ(item) ":" M_QQ(value)
#define M_VAL(item,value)   M_QQ(item) ":" value


/**************************************************************************
 * typedefs
 **************************************************************************/

typedef struct {
    char    *p_data;
    int     pos;
} write_result_t;


static uint8_t      mCmd = DCMD_NONE;
static char         mPeerAddr[INET6_ADDRSTRLEN];
static uint16_t     mPeerPort;
static char         mPeerNodeId[UCOIN_SZ_PUBKEY * 2 + 1];
static char         mBuf[BUFFER_SIZE];


/********************************************************************
 * prototypes
 ********************************************************************/

static void stop_rpc(char *pJson);
static void getinfo_rpc(char *pJson);
static void connect_rpc(char *pJson);
static void fund_rpc(char *pJson, const funding_conf_t *pFund);
static void invoice_rpc(char *pJson, uint64_t Amount);
static void listinvoice_rpc(char *pJson);
static void payment_rpc(char *pJson, const payment_conf_t *pPay);
static void close_rpc(char *pJson);

static int msg_send(char *pMsg, uint16_t Port);


/********************************************************************
 * public functions
 ********************************************************************/

int main(int argc, char *argv[])
{
#ifndef NETKIND
#error not define NETKIND
#endif
#if NETKIND==0
    ucoin_init(UCOIN_MAINNET, true);
#elif NETKIND==1
    ucoin_init(UCOIN_TESTNET, true);
#endif

    int opt;
    uint8_t options = M_OPTIONS_INIT;
    while ((opt = getopt(argc, argv, "hqlc:f:i:mp:x")) != -1) {
        switch (opt) {
        case 'h':
            options = M_OPTIONS_HELP;
            break;

        //
        // -c不要
        //
        case 'q':
            //ucoind停止
            if (options > M_OPTIONS_STOP) {
                stop_rpc(mBuf);
                options = M_OPTIONS_STOP;
            } else {
                printf("fail: too many options\n");
                options = M_OPTIONS_HELP;
            }
            break;
        case 'l':
            //channel一覧
            if (options == M_OPTIONS_INIT) {
                getinfo_rpc(mBuf);
                options = M_OPTIONS_EXEC;
            }
            break;
        case 'i':
            //payment_preimage作成
            if (options == M_OPTIONS_INIT) {
                errno = 0;
                uint64_t amount = (uint64_t)strtoull(optarg, NULL, 10);
                if (errno == 0) {
                    mCmd = DCMD_PREIMAGE;
                    invoice_rpc(mBuf, amount);
                    options = M_OPTIONS_EXEC;
                } else {
                    printf("fail: funding configuration file\n");
                    options = M_OPTIONS_HELP;
                }
            } else {
                printf("fail: too many options\n");
                options = M_OPTIONS_HELP;
            }
            break;
        case 'm':
            //payment-hash表示
            if (options == M_OPTIONS_INIT) {
                mCmd = DCMD_PAYMENT_HASH;
                listinvoice_rpc(mBuf);
                options = M_OPTIONS_EXEC;
            } else {
                printf("fail: too many options\n");
                options = M_OPTIONS_HELP;
            }
            break;

        //
        // -c必要
        //
        case 'c':
            //接続先(c,f,p共通)
            if (options > M_OPTIONS_CONN) {
                peer_conf_t peer;
                bool bret = load_peer_conf(optarg, &peer);
                if (bret) {
                    //peer.conf
                    mCmd = DCMD_CONNECT;
                    strcpy(mPeerAddr, peer.ipaddr);
                    mPeerPort = peer.port;
                    misc_bin2str(mPeerNodeId, peer.node_id, UCOIN_SZ_PUBKEY);
                    options = M_OPTIONS_CONN;
                } else {
                    printf("fail: peer configuration file\n");
                    options = M_OPTIONS_HELP;
                }
            } else {
                printf("fail: too many options\n");
                options = M_OPTIONS_HELP;
            }
            break;
        case 'f':
            //funding情報
            if (options == M_OPTIONS_CONN) {
                funding_conf_t fundconf;
                bool bret = load_funding_conf(optarg, &fundconf);
                if (bret) {
                    mCmd = DCMD_CREATE;
                    fund_rpc(mBuf, &fundconf);
                    options = M_OPTIONS_EXEC;
                } else {
                    printf("fail: funding configuration file\n");
                    options = M_OPTIONS_HELP;
                }
            } else {
                printf("-f need -c option before\n");
                options = M_OPTIONS_HELP;
            }
            break;
        case 'p':
            //payment
            if (options == M_OPTIONS_CONN) {
                payment_conf_t payconf;
                bool bret = load_payment_conf(optarg, &payconf);
                if (bret) {
                    mCmd = DCMD_PAYMENT;
                    payment_rpc(mBuf, &payconf);
                    options = M_OPTIONS_EXEC;
                } else {
                    printf("fail: payment configuration file\n");
                    options = M_OPTIONS_HELP;
                }
            } else {
                printf("-f need -c option before\n");
                options = M_OPTIONS_HELP;
            }
            break;
        case 'x':
            //mutual close
            if (options == M_OPTIONS_CONN) {
                mCmd = DCMD_CLOSE;
                close_rpc(mBuf);
                options = M_OPTIONS_EXEC;
            } else {
                printf("-x need -c option before\n");
                options = M_OPTIONS_HELP;
            }
            break;

        //
        // other
        //
        case ':':
            printf("need value: %c\n", optopt);
            options = M_OPTIONS_HELP;
            break;
        case '?':
            printf("unknown option: %c\n", optopt);
            /* THROUGH FALL */
        default:
            options = M_OPTIONS_HELP;
            break;
        }
    }

    if ((options == M_OPTIONS_INIT) || (options == M_OPTIONS_HELP) || (optind >= argc)) {
        printf("[usage]\n");
        printf("\t%s <options> <port>\n", argv[0]);
        printf("\t\t-h : help\n");
        printf("\t\t-q : quit ucoind\n");
        printf("\t\t-l : list channels\n");
        printf("\t\t-i : add preimage, and show payment_hash\n");
        printf("\t\t-m : show payment_hashs\n");
        printf("\t\t-c <node.conf> : connect node\n");
        printf("\t\t-f <fund.conf> : funding(need -c)\n");
        printf("\t\t-p <payment.conf> : payment(need -c)\n");
        printf("\t\t-x : mutual close(need -c)\n");
        return -1;
    }

    if (mCmd == DCMD_CONNECT) {
        connect_rpc(mBuf);
    }

    uint16_t port = (uint16_t)atoi(argv[optind]);

    msg_send(mBuf, port);

    ucoin_term();

    return 0;
}


/********************************************************************
 * private functions
 ********************************************************************/

static void stop_rpc(char *pJson)
{
    snprintf(pJson, BUFFER_SIZE,
        "{"
            M_STR("method", "stop") M_NEXT
            M_QQ("params") ":[]"
        "}");
}


static void getinfo_rpc(char *pJson)
{
    snprintf(pJson, BUFFER_SIZE,
        "{"
            M_STR("method", "getinfo") M_NEXT
            M_QQ("params") ":[]"
        "}");
}


static void connect_rpc(char *pJson)
{
    snprintf(pJson, BUFFER_SIZE,
        "{"
            M_STR("method", "connect") M_NEXT
            M_QQ("params") ":[ "
                //peer_nodeid, peer_addr, peer_port
                M_QQ("%s") "," M_QQ("%s") ",%d"
            " ]"
        "}",
            mPeerNodeId, mPeerAddr, mPeerPort);
}


static void fund_rpc(char *pJson, const funding_conf_t *pFund)
{
    char txid[UCOIN_SZ_TXID * 2 + 1];

    misc_bin2str_rev(txid, pFund->txid, UCOIN_SZ_TXID);
    snprintf(pJson, BUFFER_SIZE,
        "{"
            M_STR("method", "fund") M_NEXT
            M_QQ("params") ":[ "
                //peer_nodeid, peer_addr, peer_port
                M_QQ("%s") "," M_QQ("%s") ",%d,"
                //txid, txindex, signaddr, funding_sat, push_sat
                M_QQ("%s") ",%d," M_QQ("%s") ",%" PRIu64 ",%" PRIu64
            " ]"
        "}",
            mPeerNodeId, mPeerAddr, mPeerPort,
            txid, pFund->txindex, pFund->signaddr, pFund->funding_sat, pFund->push_sat);
}


static void invoice_rpc(char *pJson, uint64_t Amount)
{
    snprintf(pJson, BUFFER_SIZE,
        "{"
            M_STR("method", "invoice") M_NEXT
            M_QQ("params") ":[ "
                //invoice
                "%" PRIu64
            " ]"
        "}",
            Amount);
}


static void listinvoice_rpc(char *pJson)
{
    snprintf(pJson, BUFFER_SIZE,
        "{"
            M_STR("method", "listinvoice") M_NEXT
            M_QQ("params") ":[]"
        "}");
}


static void payment_rpc(char *pJson, const payment_conf_t *pPay)
{
    char payhash[LN_SZ_HASH * 2 + 1];
    //node_id(33*2),short_channel_id(8*2),amount(21),cltv(5)
    char forward[UCOIN_SZ_PUBKEY*2 + sizeof(uint64_t)*2 + 21 + 5 + 50];

    misc_bin2str(payhash, pPay->payment_hash, LN_SZ_HASH);
    snprintf(pJson, BUFFER_SIZE,
        "{"
            M_STR("method", "pay") M_NEXT
            M_QQ("params") ":[ "
                //peer_nodeid, peer_addr, peer_port
                M_QQ("%s") "," M_QQ("%s") ",%d,"
                //payment_hash, hop_num
                M_QQ("%s") ",%d, [\n",
            mPeerNodeId, mPeerAddr, mPeerPort,
            payhash, pPay->hop_num);

    for (int lp = 0; lp < pPay->hop_num; lp++) {
        char node_id[UCOIN_SZ_PUBKEY * 2 + 1];

        misc_bin2str(node_id, pPay->hop_datain[lp].pubkey, UCOIN_SZ_PUBKEY);
        snprintf(forward, sizeof(forward), "[" M_QQ("%s") "," M_QQ("%" PRIx64) ",%" PRIu64 ",%d]",
                node_id,
                pPay->hop_datain[lp].short_channel_id,
                pPay->hop_datain[lp].amt_to_forward,
                pPay->hop_datain[lp].outgoing_cltv_value
        );
        strcat(pJson, forward);
        if (lp != pPay->hop_num - 1) {
            strcat(pJson, ",");
        }
    }
    strcat(pJson, "] ]}");
}


static void close_rpc(char *pJson)
{
    snprintf(pJson, BUFFER_SIZE,
        "{"
            M_STR("method", "close") M_NEXT
            M_QQ("params") ":[ "
                //peer_nodeid, peer_addr, peer_port
                M_QQ("%s") "," M_QQ("%s") ",%d"
            " ]"
        "}",
            mPeerNodeId, mPeerAddr, mPeerPort);
}


static int msg_send(char *pMsg, uint16_t Port)
{
    int retval = -1;
    struct sockaddr_in sv_addr;

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return retval;
    }
    memset(&sv_addr, 0, sizeof(sv_addr));
    sv_addr.sin_family = AF_INET;
    sv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sv_addr.sin_port = htons(Port);
    retval = connect(sock, (struct sockaddr *)&sv_addr, sizeof(sv_addr));
    if (retval < 0) {
        close(sock);
        return retval;
    }
    fprintf(stderr, "------------------------\n");
    fprintf(stderr, "%s\n", pMsg);
    fprintf(stderr, "------------------------\n");
    write(sock, pMsg, strlen(pMsg));
    ssize_t len = read(sock, pMsg, BUFFER_SIZE);
    if (len > 0) {
        pMsg[len] = '\0';
        printf("%s\n", pMsg);
    }
    close(sock);

    return retval;
}
