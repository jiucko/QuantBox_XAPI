#include "stdafx.h"
#include "MdUserApi.h"
#include "../include/QueueEnum.h"
#include "../include/QueueHeader.h"

#include "../include/ApiHeader.h"
#include "../include/ApiStruct.h"

#include "../include/toolkit.h"

#include <string.h>

#include <mutex>
#include <vector>
using namespace std;

CMdUserApi::CMdUserApi(void)
{
	m_pApi = nullptr;
	m_msgQueue = nullptr;
	m_nRequestID = 0;
}

CMdUserApi::~CMdUserApi(void)
{
	Disconnect();
}

void CMdUserApi::Register(void* pMsgQueue)
{
	m_msgQueue = pMsgQueue;
}

bool CMdUserApi::IsErrorRspInfo(CSecurityFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	bool bRet = ((pRspInfo) && (pRspInfo->ErrorID != 0));
	if(bRet)
	{
		ErrorField field = { 0 };
		field.ErrorID = pRspInfo->ErrorID;
		strcpy(field.ErrorMsg, pRspInfo->ErrorMsg);

		XRespone(ResponeType::OnRtnError, m_msgQueue, this, bIsLast, 0, &field, sizeof(ErrorField), nullptr, 0, nullptr, 0);
	}
	return bRet;
}

bool CMdUserApi::IsErrorRspInfo(CSecurityFtdcRspInfoField *pRspInfo)
{
	bool bRet = ((pRspInfo) && (pRspInfo->ErrorID != 0));

	return bRet;
}

void CMdUserApi::Connect(const string& szPath,
	ServerInfoField* pServerInfo,
	UserInfoField* pUserInfo)
{
	m_szPath = szPath;
	memcpy(&m_ServerInfo, pServerInfo, sizeof(ServerInfoField));
	memcpy(&m_UserInfo, pUserInfo, sizeof(UserInfoField));

	char *pszPath = new char[szPath.length()+1024];
	srand((unsigned int)time(nullptr));
	sprintf(pszPath, "%s/%s/%s/Md/%d/", szPath.c_str(), m_ServerInfo.BrokerID, m_UserInfo.UserID, rand());
	makedirs(pszPath);

	m_pApi = CSecurityFtdcMdApi::CreateFtdcMdApi(pszPath);
	delete[] pszPath;

	XRespone(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Initialized, 0, nullptr, 0, nullptr, 0, nullptr, 0);

	if (m_pApi)
	{
		m_pApi->RegisterSpi(this);

		//添加地址
		size_t len = strlen(m_ServerInfo.Address) + 1;
		char* buf = new char[len];
		strncpy(buf, m_ServerInfo.Address, len);

		char* token = strtok(buf, _QUANTBOX_SEPS_);
		while(token)
		{
			if (strlen(token)>0)
			{
				m_pApi->RegisterFront(token);
			}
			token = strtok( nullptr, _QUANTBOX_SEPS_);
		}
		delete[] buf;

		//初始化连接
		m_pApi->Init();
		XRespone(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Connecting, 0, nullptr, 0, nullptr, 0, nullptr, 0);
	}
}

void CMdUserApi::ReqUserLogin()
{
	if (nullptr == m_pApi)
		return;

	CSecurityFtdcReqUserLoginField request = {0};

	strncpy(request.BrokerID, m_ServerInfo.BrokerID, sizeof(TSecurityFtdcBrokerIDType));
	strncpy(request.UserID, m_UserInfo.UserID, sizeof(TSecurityFtdcInvestorIDType));
	strncpy(request.Password, m_UserInfo.Password, sizeof(TSecurityFtdcPasswordType));

	//只有这一处用到了m_nRequestID，没有必要每次重连m_nRequestID都从0开始
	m_pApi->ReqUserLogin(&request,++m_nRequestID);

	XRespone(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Logining, 0, nullptr, 0, nullptr, 0, nullptr, 0);
}

void CMdUserApi::Disconnect()
{
	if(m_pApi)
	{
		m_pApi->RegisterSpi(nullptr);
		m_pApi->Release();
		m_pApi = nullptr;

		XRespone(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Disconnected, 0, nullptr, 0, nullptr, 0, nullptr, 0);
	}
}


void CMdUserApi::Subscribe(const string& szInstrumentIDs, const string& szExchageID)
{
	if(nullptr == m_pApi)
		return;

	vector<char*> vct;
	set<char*> st;

	lock_guard<mutex> cl(m_csMapInstrumentIDs);

	set<string> _setInstrumentIDs;
	map<string, set<string> >::iterator it = m_mapInstrumentIDs.find(szExchageID);
	if (it != m_mapInstrumentIDs.end())
	{
		_setInstrumentIDs = it->second;
	}

	char* pBuf = GetSetFromString(szInstrumentIDs.c_str(), _QUANTBOX_SEPS_, vct, st, 1, _setInstrumentIDs);
	m_mapInstrumentIDs[szExchageID] = _setInstrumentIDs;

	if(vct.size()>0)
	{
		//转成字符串数组
		char** pArray = new char*[vct.size()];
		for (size_t j = 0; j<vct.size(); ++j)
		{
			pArray[j] = vct[j];
		}

		//订阅
		m_pApi->SubscribeMarketData(pArray, (int)vct.size(), (char*)(szExchageID.c_str()));

		delete[] pArray;
	}
	delete[] pBuf;
}

void CMdUserApi::Subscribe(const set<string>& instrumentIDs, const string& szExchageID)
{
	if(nullptr == m_pApi)
		return;

	string szInstrumentIDs;
	for(set<string>::iterator i=instrumentIDs.begin();i!=instrumentIDs.end();++i)
	{
		szInstrumentIDs.append(*i);
		szInstrumentIDs.append(";");
	}

	if (szInstrumentIDs.length()>1)
	{
		Subscribe(szInstrumentIDs, szExchageID);
	}
}

void CMdUserApi::Unsubscribe(const string& szInstrumentIDs, const string& szExchageID)
{
	if(nullptr == m_pApi)
		return;

	vector<char*> vct;
	set<char*> st;

	lock_guard<mutex> cl(m_csMapInstrumentIDs);

	set<string> _setInstrumentIDs;
	map<string, set<string> >::iterator it = m_mapInstrumentIDs.find(szExchageID);
	if (it != m_mapInstrumentIDs.end())
	{
		_setInstrumentIDs = it->second;
	}

	char* pBuf = GetSetFromString(szInstrumentIDs.c_str(), _QUANTBOX_SEPS_, vct, st, -1, _setInstrumentIDs);
	m_mapInstrumentIDs[szExchageID] = _setInstrumentIDs;

	if(vct.size()>0)
	{
		//转成字符串数组
		char** pArray = new char*[vct.size()];
		for (size_t j = 0; j<vct.size(); ++j)
		{
			pArray[j] = vct[j];
		}

		//订阅
		m_pApi->UnSubscribeMarketData(pArray, (int)vct.size(), (char*)(szExchageID.c_str()));

		delete[] pArray;
	}
	delete[] pBuf;
}

//void CMdUserApi::SubscribeQuote(const string& szInstrumentIDs, const string& szExchageID)
//{
//	if (nullptr == m_pApi)
//		return;
//
//	vector<char*> vct;
//	set<char*> st;
//
//	lock_guard<mutex> cl(m_csMapQuoteInstrumentIDs);
//	char* pBuf = GetSetFromString(szInstrumentIDs.c_str(), _QUANTBOX_SEPS_, vct, st, 1, m_setQuoteInstrumentIDs);
//
//	if (vct.size()>0)
//	{
//		//转成字符串数组
//		char** pArray = new char*[vct.size()];
//		for (size_t j = 0; j<vct.size(); ++j)
//		{
//			pArray[j] = vct[j];
//		}
//
//		//订阅
//		m_pApi->SubscribeForQuoteRsp(pArray, (int)vct.size());
//
//		delete[] pArray;
//	}
//	delete[] pBuf;
//}
//
//void CMdUserApi::SubscribeQuote(const set<string>& instrumentIDs, const string& szExchageID)
//{
//	if (nullptr == m_pApi)
//		return;
//
//	string szInstrumentIDs;
//	for (set<string>::iterator i = instrumentIDs.begin(); i != instrumentIDs.end(); ++i)
//	{
//		szInstrumentIDs.append(*i);
//		szInstrumentIDs.append(";");
//	}
//
//	if (szInstrumentIDs.length()>1)
//	{
//		SubscribeQuote(szInstrumentIDs, szExchageID);
//	}
//}

//void CMdUserApi::UnsubscribeQuote(const string& szInstrumentIDs, const string& szExchageID)
//{
//	if (nullptr == m_pApi)
//		return;
//
//	vector<char*> vct;
//	set<char*> st;
//
//	lock_guard<mutex> cl(m_csMapQuoteInstrumentIDs);
//	char* pBuf = GetSetFromString(szInstrumentIDs.c_str(), _QUANTBOX_SEPS_, vct, st, -1, m_setQuoteInstrumentIDs);
//
//	if (vct.size()>0)
//	{
//		//转成字符串数组
//		char** pArray = new char*[vct.size()];
//		for (size_t j = 0; j<vct.size(); ++j)
//		{
//			pArray[j] = vct[j];
//		}
//
//		//订阅
//		m_pApi->UnSubscribeForQuoteRsp(pArray, (int)vct.size());
//
//		delete[] pArray;
//	}
//	delete[] pBuf;
//}

void CMdUserApi::OnFrontConnected()
{
	XRespone(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Connected, 0, nullptr, 0, nullptr, 0, nullptr, 0);

	//连接成功后自动请求登录
	ReqUserLogin();
}

void CMdUserApi::OnFrontDisconnected(int nReason)
{
	RspUserLoginField field = { 0 };
	//连接失败返回的信息是拼接而成，主要是为了统一输出
	field.ErrorID = nReason;
	GetOnFrontDisconnectedMsg(nReason, field.ErrorMsg);

	XRespone(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Disconnected, 0, &field, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
}

void CMdUserApi::OnRspUserLogin(CSecurityFtdcRspUserLoginField *pRspUserLogin, CSecurityFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	RspUserLoginField field = { 0 };

	if (!IsErrorRspInfo(pRspInfo)
		&&pRspUserLogin)
	{
		GetExchangeTime(pRspUserLogin->TradingDay, nullptr, pRspUserLogin->LoginTime,
			&field.TradingDay, nullptr, &field.LoginTime, nullptr);

		sprintf(field.SessionID, "%d:%d", pRspUserLogin->FrontID, pRspUserLogin->SessionID);

		XRespone(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Logined, 0, &field, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
		XRespone(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Done, 0, nullptr, 0, nullptr, 0, nullptr, 0);

		//有可能断线了，本处是断线重连后重新订阅
		map<string, set<string> > mapOld = m_mapInstrumentIDs;//记下上次订阅的合约

		for (map<string, set<string> >::iterator i = mapOld.begin(); i != mapOld.end(); ++i)
		{
			string strkey = i->first;
			set<string> setValue = i->second;

			Subscribe(setValue, strkey);//订阅
		}

		//有可能断线了，本处是断线重连后重新订阅
		//mapOld = m_setQuoteInstrumentIDs;//记下上次订阅的合约
		//SubscribeQuote(mapOld, "");//订阅
	}
	else
	{
		field.ErrorID = pRspInfo->ErrorID;
		strncpy(field.ErrorMsg, pRspInfo->ErrorMsg, sizeof(pRspInfo->ErrorMsg));

		XRespone(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Disconnected, 0, &field, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
	}
}

void CMdUserApi::OnRspError(CSecurityFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	IsErrorRspInfo(pRspInfo, nRequestID, bIsLast);
}

void CMdUserApi::OnRspSubMarketData(CSecurityFtdcSpecificInstrumentField *pSpecificInstrument, CSecurityFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	//在模拟平台可能这个函数不会触发，所以要自己维护一张已经订阅的合约列表
	if(!IsErrorRspInfo(pRspInfo,nRequestID,bIsLast)
		&&pSpecificInstrument)
	{
		lock_guard<mutex> cl(m_csMapInstrumentIDs);

		set<string> _setInstrumentIDs;
		map<string, set<string> >::iterator it = m_mapInstrumentIDs.find(pSpecificInstrument->ExchangeID);
		if (it != m_mapInstrumentIDs.end())
		{
			_setInstrumentIDs = it->second;
		}

		_setInstrumentIDs.insert(pSpecificInstrument->InstrumentID);
		m_mapInstrumentIDs[pSpecificInstrument->ExchangeID] = _setInstrumentIDs;
	}
}

void CMdUserApi::OnRspUnSubMarketData(CSecurityFtdcSpecificInstrumentField *pSpecificInstrument, CSecurityFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	//模拟平台可能这个函数不会触发
	if(!IsErrorRspInfo(pRspInfo,nRequestID,bIsLast)
		&&pSpecificInstrument)
	{
		lock_guard<mutex> cl(m_csMapInstrumentIDs);

		set<string> _setInstrumentIDs;
		map<string, set<string> >::iterator it = m_mapInstrumentIDs.find(pSpecificInstrument->ExchangeID);
		if (it != m_mapInstrumentIDs.end())
		{
			_setInstrumentIDs = it->second;
		}
		_setInstrumentIDs.erase(pSpecificInstrument->InstrumentID);
		m_mapInstrumentIDs[pSpecificInstrument->ExchangeID] = _setInstrumentIDs;
	}
}

//行情回调，得保证此函数尽快返回
void CMdUserApi::OnRtnDepthMarketData(CSecurityFtdcDepthMarketDataField *pDepthMarketData)
{
	DepthMarketDataField marketData = {0};
	strcpy(marketData.InstrumentID, pDepthMarketData->InstrumentID);
	strcpy(marketData.ExchangeID, pDepthMarketData->ExchangeID);

	sprintf(marketData.Symbol, "%s.%s", marketData.InstrumentID, marketData.ExchangeID);
	GetExchangeTime(pDepthMarketData->TradingDay, pDepthMarketData->ActionDay, pDepthMarketData->UpdateTime
		, &marketData.TradingDay, &marketData.ActionDay, &marketData.UpdateTime, &marketData.UpdateMillisec);
	marketData.UpdateMillisec = pDepthMarketData->UpdateMillisec;

	marketData.LastPrice = pDepthMarketData->LastPrice;
	marketData.Volume = pDepthMarketData->Volume;
	marketData.Turnover = pDepthMarketData->Turnover;
	marketData.OpenInterest = pDepthMarketData->OpenInterest;
	marketData.AveragePrice = pDepthMarketData->AveragePrice;

	marketData.OpenPrice = pDepthMarketData->OpenPrice;
	marketData.HighestPrice = pDepthMarketData->HighestPrice;
	marketData.LowestPrice = pDepthMarketData->LowestPrice;
	marketData.ClosePrice = pDepthMarketData->ClosePrice;
	marketData.SettlementPrice = pDepthMarketData->SettlementPrice;

	marketData.UpperLimitPrice = pDepthMarketData->UpperLimitPrice;
	marketData.LowerLimitPrice = pDepthMarketData->LowerLimitPrice;
	marketData.PreClosePrice = pDepthMarketData->PreClosePrice;
	marketData.PreSettlementPrice = pDepthMarketData->PreSettlementPrice;
	marketData.PreOpenInterest = pDepthMarketData->PreOpenInterest;

	marketData.BidPrice1 = pDepthMarketData->BidPrice1;
	marketData.BidVolume1 = pDepthMarketData->BidVolume1;
	marketData.AskPrice1 = pDepthMarketData->AskPrice1;
	marketData.AskVolume1 = pDepthMarketData->AskVolume1;

	//if (pDepthMarketData->BidPrice2 != DBL_MAX || pDepthMarketData->AskPrice2 != DBL_MAX)
	{
		marketData.BidPrice2 = pDepthMarketData->BidPrice2;
		marketData.BidVolume2 = pDepthMarketData->BidVolume2;
		marketData.AskPrice2 = pDepthMarketData->AskPrice2;
		marketData.AskVolume2 = pDepthMarketData->AskVolume2;

		marketData.BidPrice3 = pDepthMarketData->BidPrice3;
		marketData.BidVolume3 = pDepthMarketData->BidVolume3;
		marketData.AskPrice3 = pDepthMarketData->AskPrice3;
		marketData.AskVolume3 = pDepthMarketData->AskVolume3;

		marketData.BidPrice4 = pDepthMarketData->BidPrice4;
		marketData.BidVolume4 = pDepthMarketData->BidVolume4;
		marketData.AskPrice4 = pDepthMarketData->AskPrice4;
		marketData.AskVolume4 = pDepthMarketData->AskVolume4;

		marketData.BidPrice5 = pDepthMarketData->BidPrice5;
		marketData.BidVolume5 = pDepthMarketData->BidVolume5;
		marketData.AskPrice5 = pDepthMarketData->AskPrice5;
		marketData.AskVolume5 = pDepthMarketData->AskVolume5;
	}

	XRespone(ResponeType::OnRtnDepthMarketData, m_msgQueue, this, 0, 0, &marketData, sizeof(DepthMarketDataField), nullptr, 0, nullptr, 0);
}

//void CMdUserApi::OnRspSubForQuoteRsp(CSecurityFtdcSpecificInstrumentField *pSpecificInstrument, CSecurityFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
//{
//	if (!IsErrorRspInfo(pRspInfo, nRequestID, bIsLast)
//		&& pSpecificInstrument)
//	{
//		lock_guard<mutex> cl(m_csMapQuoteInstrumentIDs);
//
//		m_setQuoteInstrumentIDs.insert(pSpecificInstrument->InstrumentID);
//	}
//}
//
//void CMdUserApi::OnRspUnSubForQuoteRsp(CSecurityFtdcSpecificInstrumentField *pSpecificInstrument, CSecurityFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
//{
//	if (!IsErrorRspInfo(pRspInfo, nRequestID, bIsLast)
//		&& pSpecificInstrument)
//	{
//		lock_guard<mutex> cl(m_csMapQuoteInstrumentIDs);
//
//		m_setQuoteInstrumentIDs.erase(pSpecificInstrument->InstrumentID);
//	}
//}

//void CMdUserApi::OnRtnForQuoteRsp(CSecurityFtdcForQuoteRspField *pForQuoteRsp)
//{
//	//if (m_msgQueue)
//	//	m_msgQueue->Input_OnRtnForQuoteRsp(this, pForQuoteRsp);
//
//
//	//XCall(m_msgQueue, ResponeType::OnConnectionStatus, 0, 0, 0, this, &field, nullptr);
//}
