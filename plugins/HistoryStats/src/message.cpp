#include "stdafx.h"
#include "message.h"

#include <algorithm>

/*
 * Message
 */

void Message::makeRawAvailable()
{
	do {
		if (m_Available & PtrIsNonT) {
			m_Raw = utils::fromA(ext::a::string(reinterpret_cast<const char*>(m_RawSource), m_nLength));
			m_Available |= Raw;
			break;
		}

		if (m_Available & PtrIsUTF8) {
			m_Raw = utils::fromUTF8(reinterpret_cast<const char*>(m_RawSource));
			m_Available |= Raw;
			break;
		}

		m_Raw.assign(reinterpret_cast<const wchar_t*>(m_RawSource), m_nLength);
		m_Available |= Raw;
	}
	while (false);

	if (m_bStripRawRTF)
		stripRawRTF();

	if (m_bStripBBCodes)
		stripBBCodes();
}

void Message::stripRawRTF()
{
	if (m_Raw.substr(0, 6) == L"{\\rtf1")
		m_Raw = RTFFilter::filter(m_Raw);
}

void Message::stripBBCodes()
{
	static const wchar_t* szSimpleBBCodes[][2] = {
		{ L"[b]", L"[/b]" },
		{ L"[u]", L"[/u]" },
		{ L"[i]", L"[/i]" },
		{ L"[s]", L"[/s]" },
	};

	static const wchar_t* szParamBBCodes[][2] = {
		{ L"[url=", L"[/url]" },
		{ L"[color=", L"[/color]" },
	};

	// convert raw string to lower case
	ext::string strRawLC = utils::toLowerCase(m_Raw);

	// remove simple BBcodes
	array_each_(i, szSimpleBBCodes)
	{
		const wchar_t* szOpenTag = szSimpleBBCodes[i][0];
		const wchar_t* szCloseTag = szSimpleBBCodes[i][1];

		int lenOpen = ext::strfunc::len(szOpenTag);
		int lenClose = ext::strfunc::len(szCloseTag);

		size_t posOpen = 0;
		size_t posClose = 0;

		while (true) {
			if ((posOpen = strRawLC.find(szOpenTag, posOpen)) == ext::string::npos)
				break;

			if ((posClose = strRawLC.find(szCloseTag, posOpen + lenOpen)) == ext::string::npos)
				break;

			strRawLC.erase(posOpen, lenOpen);
			strRawLC.erase(posClose - lenOpen, lenClose);

			// fix real string
			m_Raw.erase(posOpen, lenOpen);
			m_Raw.erase(posClose - lenOpen, lenClose);
		}
	}

	// remove BBcodes with parameters
	array_each_(i, szParamBBCodes)
	{
		const wchar_t* szOpenTag = szParamBBCodes[i][0];
		const wchar_t* szCloseTag = szParamBBCodes[i][1];

		int lenOpen = ext::strfunc::len(szOpenTag);
		int lenClose = ext::strfunc::len(szCloseTag);

		size_t posOpen = 0;
		size_t posOpen2 = 0;
		size_t posClose = 0;

		while (true) {
			if ((posOpen = strRawLC.find(szOpenTag, posOpen)) == ext::string::npos)
				break;

			if ((posOpen2 = strRawLC.find(']', posOpen + lenOpen)) == ext::string::npos)
				break;

			if ((posClose = strRawLC.find(szCloseTag, posOpen2 + 1)) == ext::string::npos)
				break;

			strRawLC.erase(posOpen, posOpen2 - posOpen + 1);
			strRawLC.erase(posClose - posOpen2 + posOpen - 1, lenClose);

			// fix real string
			m_Raw.erase(posOpen, posOpen2 - posOpen + 1);
			m_Raw.erase(posClose - posOpen2 + posOpen - 1, lenClose);
		}
	}
}

void Message::filterLinks()
{
	static const wchar_t* szSpaces = L" \r\r\n";
	static const wchar_t* szPrefixes = L"([{<:\"'";
	static const wchar_t* szSuffixes = L".,:;!?)]}>\"'";
	static const wchar_t* szValidProtocol = L"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	static const wchar_t* szValidHost = L".-abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	// init with raw text
	m_WithoutLinks = getRaw();

	ext::string& msg = m_WithoutLinks;
	size_t pos = -1;

	// detect: protocol://[user[:password]@]host[/path]
	while (true) {
		if ((pos = msg.find(L"://", pos + 1)) == ext::string::npos)
			break;

		// find start of URL
		size_t pos_proto = msg.find_last_not_of(szValidProtocol, pos - 1);

		(pos_proto == ext::string::npos) ? pos_proto = 0 : ++pos_proto;

		if (pos_proto < pos) {
			// find end of URL
			size_t pos_last = msg.find_first_of(szSpaces, pos + 3);

			(pos_last == ext::string::npos) ? pos_last = msg.length() - 1 : --pos_last;

			// filter suffixes (punctuation, parentheses, ...)
			if (ext::strfunc::chr(szSuffixes, msg[pos_last]))
				--pos_last;

			// find slash: for host name validation
			size_t pos_slash = msg.find('/', pos + 3);

			if (pos_slash == ext::string::npos || pos_slash > pos_last)
				pos_slash = pos_last + 1;

			// find at: for host name validation
			size_t pos_at = msg.find('@', pos + 3);

			if (pos_at == ext::string::npos || pos_at > pos_slash)
				pos_at = pos + 2;

			// check for valid host (x.x)
			if (pos_slash - pos_at > 3) {
				size_t pos_invalid = msg.find_first_not_of(szValidHost, pos_at + 1);

				if (pos_invalid == ext::string::npos || pos_invalid >= pos_slash) {
					if (std::count(msg.begin() + pos_at + 1, msg.begin() + pos_slash, '.') >= 1) {
						ext::string link = msg.substr(pos_proto, pos_last - pos_proto + 1);

						// remove extracted link from message text
						msg.erase(pos_proto, link.length());
						pos = pos_last - link.length();

						// TODO: put link in list
					}
				}
			}
		}
	}

	// detect: www.host[/path]
	pos = -1;

	while (true) {
		if ((pos = msg.find(L"www.", pos + 1)) == ext::string::npos)
			break;

		// find end of URL
		size_t pos_last = msg.find_first_of(szSpaces, pos + 4);

		(pos_last == ext::string::npos) ? pos_last = msg.length() - 1 : --pos_last;

		// filter suffixes (punctuation, parentheses, ...)
		if (ext::strfunc::chr(szSuffixes, msg[pos_last]))
			--pos_last;

		// find slash: for host name validation
		size_t pos_slash = msg.find('/', pos + 4);

		if (pos_slash == ext::string::npos || pos_slash > pos_last)
			pos_slash = pos_last + 1;

		// find at: for host name validation
		size_t pos_at = pos + 3;

		// check for valid host (x.x)
		if (pos_slash - pos_at > 3) {
			size_t pos_invalid = msg.find_first_not_of(szValidHost, pos_at + 1);

			if (pos_invalid == ext::string::npos || pos_invalid >= pos_slash) {
				if (std::count(msg.begin() + pos_at + 1, msg.begin() + pos_slash, '.') >= 1) {
					ext::string link = L"http://" + msg.substr(pos, pos_last - pos + 1);

					// remove extracted link from message text
					msg.erase(pos, link.length() - 7);
					pos = pos_last - (link.length() - 7);

					// TODO: put link in list
				}
			}
		}
	}

	// detect: user@host
	pos = -1;

	while (true) {
		if ((pos = msg.find('@', pos + 1)) == ext::string::npos)
			break;

		if (pos > 0 && pos < msg.length() - 1) {
			// find end of address
			size_t pos_last = msg.find_first_not_of(szValidHost, pos + 1);

			(pos_last == ext::string::npos) ? pos_last = msg.length() - 1 : --pos_last;

			// filter suffixes (punctuation, parentheses, ...)
			if (ext::strfunc::chr(szSuffixes, msg[pos_last]))
				--pos_last;

			// find start of address
			size_t pos_first = msg.find_last_of(szSpaces, pos - 1);

			(pos_first == ext::string::npos) ? pos_first = 0 : ++pos_first;

			// filter prefixes (punctuation, parentheses, ...)
			if (ext::strfunc::chr(szPrefixes, msg[pos_first]))
				++pos_first;

			// check for valid host (x.x)
			if (pos_first < pos && pos_last - pos >= 3) {
				if (std::count(msg.begin() + pos + 1, msg.begin() + pos_last + 1, '.') >= 1) {
					ext::string link = msg.substr(pos_first, pos_last - pos_first + 1);

					// remove extracted link from message text
					msg.erase(pos_first, link.length());
					pos = pos_last - (link.length());

					// prepend "mailto:" if missing
					if (link.substr(0, 7) != L"mailto:") {
						link.insert(0, L"mailto:");
					}

					// TODO: put link in list
				}
			}
		}
	}

	m_Available |= WithoutLinks;
}
