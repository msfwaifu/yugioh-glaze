#include <QDebug>
#include <QEventLoop>
#include <time.h>
#include "game.h"
#include "singlemode.h"
#include "mtrandom.h"
#include "ocgapi.h"
#include "datamanager.h"
#include "bufferio.h"
#include "field.h"
#include "duelclient.h"

namespace glaze {

long SingleMode::pduel = 0;
bool SingleMode::is_closing = false;
bool SingleMode::is_continuing = false;
wchar_t SingleMode::event_string[256];
int SingleMode::enable_log = 2;
QString SingleMode::name = "";

SingleMode::SingleMode()
{

}

void SingleMode::SetResponse(unsigned char* resp) {
    if(!pduel)
        return;
    set_responseb(pduel, resp);
}

void SingleMode::singlePlayStart()
{
    qDebug()<<"SingleMode run called from?: "<<QThread::currentThreadId();
    QString fname("./assets/single/"+name+".lua");
    char fname2[256];
    QEventLoop loop;
    connect(&mainGame->dField, SIGNAL(clearFinished()), &loop, SLOT(quit()));
    strcpy(fname2, fname.toStdString().c_str());
    size_t slen = fname.length();
    qDebug()<<"SingleMode game name after conversion is "<<fname<<" and length is "<<slen;
    qDebug()<<"script name is "<<fname2;
    mtrandom rnd;
    time_t seed = time(0);
    rnd.reset(seed);
    set_card_reader((card_reader)DataManager::CardReader);
    set_message_handler((message_handler)MessageHandler);
    pduel = create_duel(rnd.rand());    
    set_player_info(pduel, 0, 8000, 5, 1);
    set_player_info(pduel, 1, 8000, 5, 1);
    qDebug()<<"card info set. Duel created in ocgcore";
    mainGame->dInfo.lp[0] = 8000;
    mainGame->dInfo.lp[1] = 8000;
//    myswprintf(mainGame->dInfo.strLP[0], L"%d", mainGame->dInfo.lp[0]);
//    myswprintf(mainGame->dInfo.strLP[1], L"%d", mainGame->dInfo.lp[1]);
//    BufferIO::CopyWStr(mainGame->ebNickName->getText(), mainGame->dInfo.hostname, 20);
    mainGame->dInfo.clientname[0] = 0;
    mainGame->dInfo.turn = 0;
//    mainGame->dInfo.strTurn[0] = 0;
    qDebug()<<"Duel info set";

    if(!preload_script(pduel, fname2, slen)) {  //loading the lua script for puzzle in ocgcore
        end_duel(pduel);
        qDebug()<<"SingleMode script load ERROR";
        return ;
    }
    qDebug()<<"SingleMode game script loaded successfully";
    //Initialze the field to start the game
    mainGame->dInfo.is_first = true;
    mainGame->dInfo.is_started = true;
    mainGame->dInfo.is_singleMode = true;
    mainGame->dField.hovered_card = 0;
    mainGame->dField.clicked_card = 0;
//    mainGame->dField.Clear();
    QMetaObject::invokeMethod(&mainGame->dField,"Clear",Qt::QueuedConnection);
    loop.exec();

    start_duel(pduel, 0);
    char engineBuffer[0x1000];  //size = 4096
    is_closing = false;
    is_continuing = true;
    int len = 0;

    while (is_continuing) {
        int result = process(pduel);    //function from ocgcore
        len = result & 0xffff;
        /* int flag = result >> 16; */
        if (len > 0) {
            get_message(pduel, (byte*)engineBuffer);    //function from ocgcore which return array containing commands of current duel process
            is_continuing = SinglePlayAnalyze(engineBuffer, len);   // the commands from above function are processed here
        }
    }
//    mainGame->dField.Clear();
    QMetaObject::invokeMethod(&mainGame->dField,"Clear",Qt::QueuedConnection);
    loop.exec();
    qDebug()<<"SingleMode game ended";
    end_duel(pduel);
    if(is_closing) {
        //Game ended; do the necessary resetting
        mainGame->dInfo.is_started = false;
        mainGame->dInfo.is_singleMode = false;
    }
    emit finished();
}

bool SingleMode::SinglePlayAnalyze(char* msg, unsigned int len)
{
    char* offset, *pbuf = msg;
    int player, count;
    QEventLoop loop;
    connect(&mainGame->dField, SIGNAL(clearFinished()), &loop, SLOT(quit()));
    connect(&mainGame->dField, SIGNAL(addCardFinished()), &loop, SLOT(quit()));
    connect(&mainGame->dField, SIGNAL(singlePlayRefreshFinished()), &loop, SLOT(quit()));
    connect(&mainGame->dField, SIGNAL(singlePlayRefreshHandFinished()), &loop, SLOT(quit()));
    connect(&mainGame->dField, SIGNAL(singlePlayRefreshGraveFinished()), &loop, SLOT(quit()));
    connect(&mainGame->dField, SIGNAL(singlePlayRefreshDeckFinished()), &loop, SLOT(quit()));
    connect(&mainGame->dField, SIGNAL(singlePlayRefreshExtraFinished()), &loop, SLOT(quit()));
    connect(&mainGame->dField, SIGNAL(singlePlayRefreshSingleFinished()), &loop, SLOT(quit()));
    connect(&mainGame->dField, SIGNAL(singlePlayReloadFinished()), &loop, SLOT(quit()));

    while (pbuf - msg < (int)len) {
        if(is_closing || !is_continuing)
            return false;
        offset = pbuf;
        mainGame->dInfo.curMsg = BufferIO::ReadUInt8(pbuf);
        qDebug()<<"CURRENT DUEL MESSAGE IS "<<mainGame->dInfo.curMsg;
        switch (mainGame->dInfo.curMsg) {
        case MSG_RETRY: {
            qDebug()<<"MSG_RETRY entered";
            mainGame->wMessage = true;
            mainGame->stMessage = "Error occured.";
            emit mainGame->qwMessageChanged();
            mainGame->actionSignal.reset();
            mainGame->actionSignal.wait();
            return false;
        }
        case MSG_HINT: {
            /*int type = */BufferIO::ReadInt8(pbuf);
            int player = BufferIO::ReadInt8(pbuf);
            /*int data = */BufferIO::ReadInt32(pbuf);
            if(player == 0)
                DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_WIN: {
            pbuf += 2;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            return false;
        }
        case MSG_SELECT_BATTLECMD: {
            player = BufferIO::ReadInt8(pbuf);
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 11;
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 8 + 2;
//            SinglePlayRefresh();
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefresh",Qt::QueuedConnection);
            loop.exec();
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            break;
        }
        case MSG_SELECT_IDLECMD: {
            qDebug()<<"MSG_SELECT_IDLECMD enter";
            player = BufferIO::ReadInt8(pbuf);
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 7;
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 7;
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 7;
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 7;
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 7;
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 11 + 2;
//            SinglePlayRefresh();
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefresh",Qt::QueuedConnection);
            loop.exec();
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            qDebug()<<"MSG_SELECT_IDLECMD exit";
            break;
        }
        case MSG_SELECT_EFFECTYN: {
            player = BufferIO::ReadInt8(pbuf);
            pbuf += 8;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            break;
        }
        case MSG_SELECT_YESNO: {
            player = BufferIO::ReadInt8(pbuf);
            pbuf += 4;
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            break;
        }
        case MSG_SELECT_OPTION: {
            player = BufferIO::ReadInt8(pbuf);
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 4;
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            break;
        }
        case MSG_SELECT_CARD:
        case MSG_SELECT_TRIBUTE: {
            player = BufferIO::ReadInt8(pbuf);
            pbuf += 3;
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 8;
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            break;
        }
        case MSG_SELECT_CHAIN: {
            player = BufferIO::ReadInt8(pbuf);
            count = BufferIO::ReadInt8(pbuf);
            pbuf += 10 + count * 12;
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            break;
        }
        case MSG_SELECT_PLACE:
        case MSG_SELECT_DISFIELD: {
            player = BufferIO::ReadInt8(pbuf);
            pbuf += 5;
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            break;
        }
        case MSG_SELECT_POSITION: {
            player = BufferIO::ReadInt8(pbuf);
            pbuf += 5;
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            break;
        }
        case MSG_SELECT_COUNTER: {
            player = BufferIO::ReadInt8(pbuf);
            pbuf += 3;
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 8;
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            break;
        }
        case MSG_SELECT_SUM: {
            pbuf++;
            player = BufferIO::ReadInt8(pbuf);
            pbuf += 6;
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 11;
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            break;
        }
        case MSG_SORT_CARD:
        case MSG_SORT_CHAIN: {
            player = BufferIO::ReadInt8(pbuf);
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 7;
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            break;
        }
        case MSG_CONFIRM_DECKTOP: {
            player = BufferIO::ReadInt8(pbuf);
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 7;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_CONFIRM_CARDS: {
            player = BufferIO::ReadInt8(pbuf);
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 7;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_SHUFFLE_DECK: {
            player = BufferIO::ReadInt8(pbuf);
            DuelClient::ClientAnalyze(offset, pbuf - offset);
//            SinglePlayRefreshDeck(player);
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefreshDeck",Qt::QueuedConnection,
                                      Q_ARG(int, player));
            loop.exec();
            break;
        }
        case MSG_SHUFFLE_HAND: {
            /*int oplayer = */BufferIO::ReadInt8(pbuf);
            int count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 4;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_REFRESH_DECK: {
            pbuf++;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_SWAP_GRAVE_DECK: {
            player = BufferIO::ReadInt8(pbuf);
            DuelClient::ClientAnalyze(offset, pbuf - offset);
//            SinglePlayRefreshGrave(player);
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefreshGrave",Qt::QueuedConnection,
                                      Q_ARG(int, player));
            loop.exec();
            break;
        }
        case MSG_REVERSE_DECK: {
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_DECK_TOP: {
            pbuf += 6;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_SHUFFLE_SET_CARD: {
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 8;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_NEW_TURN: {
            player = BufferIO::ReadInt8(pbuf);
            DuelClient::ClientAnalyze(offset, pbuf - offset);     //TODO: causing crash
            break;
        }
        case MSG_NEW_PHASE: {
            pbuf++;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
//            SinglePlayRefresh();
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefresh",Qt::QueuedConnection);
            loop.exec();
            break;
        }
        case MSG_MOVE: {
            int pc = pbuf[4];
            int pl = pbuf[5];
            /*int ps = pbuf[6];*/
            /*int pp = pbuf[7];*/
            int cc = pbuf[8];
            int cl = pbuf[9];
            int cs = pbuf[10];
            /*int cp = pbuf[11];*/
            pbuf += 16;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            if(cl && !(cl & 0x80) && (pl != cl || pc != cc))
//                SinglePlayRefreshSingle(cc, cl, cs);
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefreshSingle",Qt::QueuedConnection,
                                      Q_ARG(int, cc),
                                      Q_ARG(int, cl),
                                      Q_ARG(int, cs));
            loop.exec();
            break;
        }
        case MSG_POS_CHANGE: {
            pbuf += 9;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_SET: {
            pbuf += 8;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_SWAP: {
            pbuf += 16;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_FIELD_DISABLED: {
            pbuf += 4;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_SUMMONING: {
            pbuf += 8;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_SUMMONED: {
            DuelClient::ClientAnalyze(offset, pbuf - offset);
//            SinglePlayRefresh();
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefresh",Qt::QueuedConnection);
            loop.exec();
            break;
        }
        case MSG_SPSUMMONING: {
            pbuf += 8;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_SPSUMMONED: {
            DuelClient::ClientAnalyze(offset, pbuf - offset);
//            SinglePlayRefresh();
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefresh",Qt::QueuedConnection);
            loop.exec();
            break;
        }
        case MSG_FLIPSUMMONING: {
            pbuf += 8;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_FLIPSUMMONED: {
            DuelClient::ClientAnalyze(offset, pbuf - offset);
//            SinglePlayRefresh();
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefresh",Qt::QueuedConnection);
            loop.exec();
            break;
        }
        case MSG_CHAINING: {
            pbuf += 16;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_CHAINED: {
            pbuf++;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
//            SinglePlayRefresh();
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefresh",Qt::QueuedConnection);
            loop.exec();
            break;
        }
        case MSG_CHAIN_SOLVING: {
            pbuf++;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_CHAIN_SOLVED: {
            pbuf++;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
//            SinglePlayRefresh();
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefresh",Qt::QueuedConnection);
            loop.exec();
            break;
        }
        case MSG_CHAIN_END: {
            DuelClient::ClientAnalyze(offset, pbuf - offset);
//            SinglePlayRefresh();
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefresh",Qt::QueuedConnection);
            loop.exec();
//            SinglePlayRefreshDeck(0);
//            SinglePlayRefreshDeck(1);
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefreshDeck",Qt::QueuedConnection,
                                      Q_ARG(int, 0));
            loop.exec();
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefreshDeck",Qt::QueuedConnection,
                                      Q_ARG(int, 1));
            loop.exec();
            break;
        }
        case MSG_CHAIN_NEGATED: {
            pbuf++;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_CHAIN_DISABLED: {
            pbuf++;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_CARD_SELECTED:
        case MSG_RANDOM_SELECTED: {
            player = BufferIO::ReadInt8(pbuf);
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 4;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_BECOME_TARGET: {
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 4;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_DRAW: {
            player = BufferIO::ReadInt8(pbuf);
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count * 4;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_DAMAGE: {
            pbuf += 5;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_RECOVER: {
            pbuf += 5;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_EQUIP: {
            pbuf += 8;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_LPUPDATE: {
            pbuf += 5;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_UNEQUIP: {
            pbuf += 4;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_CARD_TARGET: {
            pbuf += 8;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_CANCEL_TARGET: {
            pbuf += 8;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_PAY_LPCOST: {
            pbuf += 5;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_ADD_COUNTER: {
            pbuf += 6;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_REMOVE_COUNTER: {
            pbuf += 6;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_ATTACK: {
            pbuf += 8;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_BATTLE: {
            pbuf += 26;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_ATTACK_DISABLED: {
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_DAMAGE_STEP_START: {
            DuelClient::ClientAnalyze(offset, pbuf - offset);
//            SinglePlayRefresh();
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefresh",Qt::QueuedConnection);
            loop.exec();
            break;
        }
        case MSG_DAMAGE_STEP_END: {
            DuelClient::ClientAnalyze(offset, pbuf - offset);
//            SinglePlayRefresh();
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefresh",Qt::QueuedConnection);
            loop.exec();
            break;
        }
        case MSG_MISSED_EFFECT: {
            pbuf += 8;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_TOSS_COIN: {
            player = BufferIO::ReadInt8(pbuf);
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_TOSS_DICE: {
            player = BufferIO::ReadInt8(pbuf);
            count = BufferIO::ReadInt8(pbuf);
            pbuf += count;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_ANNOUNCE_RACE: {
            player = BufferIO::ReadInt8(pbuf);
            pbuf += 5;
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            break;
        }
        case MSG_ANNOUNCE_ATTRIB: {
            player = BufferIO::ReadInt8(pbuf);
            pbuf += 5;
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            break;
        }
        case MSG_ANNOUNCE_CARD: {
            player = BufferIO::ReadInt8(pbuf);
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            break;
        }
        case MSG_ANNOUNCE_NUMBER: {
            player = BufferIO::ReadInt8(pbuf);
            count = BufferIO::ReadInt8(pbuf);
            pbuf += 4 * count;
            if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
                mainGame->singleSignal.reset();
                mainGame->singleSignal.wait();
            }
            break;
        }
        case MSG_CARD_HINT: {
            pbuf += 9;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
            break;
        }
        case MSG_TAG_SWAP: {
            player = pbuf[0];
            pbuf += pbuf[3] * 4 + 8;
            DuelClient::ClientAnalyze(offset, pbuf - offset);
//            SinglePlayRefreshDeck(player);
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefreshDeck",Qt::QueuedConnection,
                                      Q_ARG(int, player));
            loop.exec();
//            SinglePlayRefreshExtra(player);
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayRefreshExtra",Qt::QueuedConnection,
                                      Q_ARG(int, player));
            loop.exec();
            break;
        }
        case MSG_MATCH_KILL: {
            pbuf += 4;
            break;
        }
        case MSG_RELOAD_FIELD: {
            qDebug()<<"MSG_RELOAD_FIELD enter";
//            mainGame->dField.Clear();
            QMetaObject::invokeMethod(&mainGame->dField,"Clear",Qt::QueuedConnection);
            loop.exec();

            int val = 0;
            for(int p = 0; p < 2; ++p) {
                mainGame->dInfo.lp[p] = BufferIO::ReadInt32(pbuf);
                qDebug()<<"Life point of player "<<p+1<<" set to "<<mainGame->dInfo.lp[p];
                for(int seq = 0; seq < 5; ++seq) {
                    val = BufferIO::ReadInt8(pbuf);
                    if(val) {
                        ClientCard* ccard = new ClientCard;
						ClientCard* tmp = ccard;
//                        mainGame->dField.AddCard(p, LOCATION_MZONE, seq);
                        QMetaObject::invokeMethod(&mainGame->dField,"AddCard",Qt::QueuedConnection,
                                                  Q_ARG(int, p),
                                                  Q_ARG(int, LOCATION_MZONE),
                                                  Q_ARG(int, seq),
                                                  Q_ARG(ClientCard*, 0),
                                                  Q_ARG(ClientCard**, &ccard));
                        loop.exec();
						delete tmp;	// ccard reallocated in the above function
                        ccard->position = BufferIO::ReadInt8(pbuf);
                        val = BufferIO::ReadInt8(pbuf);
                        if(val) {
                            for(int xyz = 0; xyz < val; ++xyz) {
                                ClientCard* xcard = new ClientCard;
                                ccard->overlayed.push_back(xcard);
                                mainGame->dField.overlay_cards.insert(xcard);
                                xcard->overlayTarget = ccard;
                                xcard->location = 0x80;
                                xcard->sequence = ccard->overlayed.size() - 1;
                            }
                        }
                    }
                }
                for(int seq = 0; seq < 8; ++seq) {
                    val = BufferIO::ReadInt8(pbuf);
                    if(val) {
                        ClientCard* ccard = new ClientCard;
						ClientCard* tmp = ccard;
//                        mainGame->dField.AddCard(ccard, p, LOCATION_SZONE, seq);
                        QMetaObject::invokeMethod(&mainGame->dField,"AddCard",Qt::QueuedConnection,
                                                  Q_ARG(int, p),
                                                  Q_ARG(int, LOCATION_SZONE),
                                                  Q_ARG(int, seq),
                                                  Q_ARG(ClientCard*, 0),
                                                  Q_ARG(ClientCard**, &ccard));
                        loop.exec();
						delete tmp;
                        ccard->position = BufferIO::ReadInt8(pbuf);
                    }
                }
                val = BufferIO::ReadInt8(pbuf);
                for(int seq = 0; seq < val; ++seq) {
//                    ClientCard* ccard = new ClientCard();
//                    mainGame->dField.AddCard(ccard, p, LOCATION_DECK, seq);
                    QMetaObject::invokeMethod(&mainGame->dField,"AddCard",Qt::QueuedConnection,
                                              Q_ARG(int, p),
                                              Q_ARG(int, LOCATION_DECK),
                                              Q_ARG(int, seq));
                    loop.exec();

                }
                val = BufferIO::ReadInt8(pbuf);
                for(int seq = 0; seq < val; ++seq) {
//                    ClientCard* ccard = new ClientCard;
//                    mainGame->dField.AddCard(ccard, p, LOCATION_HAND, seq);
                    QMetaObject::invokeMethod(&mainGame->dField,"AddCard",Qt::QueuedConnection,
                                              Q_ARG(int, p),
                                              Q_ARG(int, LOCATION_HAND),
                                              Q_ARG(int, seq));
                    loop.exec();
                }
                val = BufferIO::ReadInt8(pbuf);
                for(int seq = 0; seq < val; ++seq) {
//                    ClientCard* ccard = new ClientCard;
//                    mainGame->dField.AddCard(ccard, p, LOCATION_GRAVE, seq);
                    QMetaObject::invokeMethod(&mainGame->dField,"AddCard",Qt::QueuedConnection,
                                              Q_ARG(int, p),
                                              Q_ARG(int, LOCATION_GRAVE),
                                              Q_ARG(int, seq));
                    loop.exec();
                }
                val = BufferIO::ReadInt8(pbuf);
                for(int seq = 0; seq < val; ++seq) {
//                    ClientCard* ccard = new ClientCard;
//                    mainGame->dField.AddCard(ccard, p, LOCATION_REMOVED, seq);
                    QMetaObject::invokeMethod(&mainGame->dField,"AddCard",Qt::QueuedConnection,
                                              Q_ARG(int, p),
                                              Q_ARG(int, LOCATION_REMOVED),
                                              Q_ARG(int, seq));
                    loop.exec();
                }
                val = BufferIO::ReadInt8(pbuf);
                for(int seq = 0; seq < val; ++seq) {
//                    ClientCard* ccard = new ClientCard;
//                    mainGame->dField.AddCard(ccard, p, LOCATION_EXTRA, seq);
                    QMetaObject::invokeMethod(&mainGame->dField,"AddCard",Qt::QueuedConnection,
                                              Q_ARG(int, p),
                                              Q_ARG(int, LOCATION_EXTRA),
                                              Q_ARG(int, seq));
                    loop.exec();
                }
            }
            BufferIO::ReadInt8(pbuf); //chain count, always 0
//            SinglePlayReload();
            QMetaObject::invokeMethod(&mainGame->dField,"SinglePlayReload",Qt::QueuedConnection);
            qDebug()<<"SinglePlayReload queued from"<<QThread::currentThreadId();
            loop.exec();
//            mainGame->dField.RefreshAllCards(); // updates the position and rotation
            emit mainGame->dInfo.lp1Changed();
            emit mainGame->dInfo.lp2Changed();
            qDebug()<<"MSG_RELOAD_FIELD exit";
            break;
        }
        case MSG_AI_NAME: {
            char namebuf[128];
            wchar_t wname[128];
            int len = BufferIO::ReadInt16(pbuf);
            char* begin = pbuf;
            pbuf += len + 1;
            memcpy(namebuf, begin, len + 1);
            BufferIO::DecodeUTF8(namebuf, wname);
            BufferIO::CopyWStr(wname, mainGame->dInfo.clientname, 20);
            emit mainGame->dInfo.clientNameChanged();
            break;
        }
        case MSG_SHOW_HINT: {
            char msgbuf[1024];
            wchar_t msg[1024];
            int len = BufferIO::ReadInt16(pbuf);
            char* begin = pbuf;
            pbuf += len + 1;
            memcpy(msgbuf, begin, len + 1);
            BufferIO::DecodeUTF8(msgbuf, msg);
            mainGame->stMessage = QString::fromWCharArray(msg);
            mainGame->wMessage = true ;
            emit mainGame->qwMessageChanged();
            mainGame->actionSignal.reset();
            mainGame->actionSignal.wait();
            break;
        }
        }
    }
    return is_continuing;
}

int SingleMode::MessageHandler(long fduel, int type) {
    if(!enable_log)
        return 0;
    char msgbuf[1024];
    get_log_message(fduel, (byte*)msgbuf);
    if(enable_log == 1) {
        wchar_t wbuf[1024];
        BufferIO::DecodeUTF8(msgbuf, wbuf);
        mainGame->AddChatMsg(wbuf, 9);
    } else if(enable_log == 2) {
        FILE* fp = fopen("error.log", "at");
        if(!fp)
            return 0;
        fprintf(fp, "[Script error:] %s\n", msgbuf);
        fclose(fp);
    }
    return 0;
}

}
