#include "musicdownloadquerykwthread.h"
#include "musicnumberutils.h"
#include "musictime.h"
#///QJson import
#include "qjson/parser.h"

MusicKWMusicInfoConfigManager::MusicKWMusicInfoConfigManager(QObject *parent)
    : MusicAbstractXml(parent)
{

}

QString MusicKWMusicInfoConfigManager::getClassName()
{
    return staticMetaObject.className();
}

void MusicKWMusicInfoConfigManager::readMusicInfoConfig(MusicObject::MusicSongInformation *info)
{
    info->m_singerName = readXmlTextByTagName("artist");
    info->m_songName = readXmlTextByTagName("name");
    info->m_songId = readXmlTextByTagName("music_id");
    info->m_artistId = readXmlTextByTagName("albid");
    info->m_albumId = readXmlTextByTagName("artid");
    info->m_albumName = readXmlTextByTagName("special");

    QString mp3Url = readXmlTextByTagName("mp3dl");
    if(!mp3Url.isEmpty())
    {
        QString v = readXmlTextByTagName("mp3path");
        if(!v.isEmpty())
        {
            MusicObject::MusicSongAttribute attr;
            attr.m_bitrate = MB_128;
            attr.m_format = MP3_FILE_PREFIX;
            attr.m_size = "-";
            attr.m_url = QString("http://%1/resource/%2").arg(mp3Url).arg(v);
            info->m_songAttrs.append(attr);
        }

        v = readXmlTextByTagName("path");
        if(!v.isEmpty())
        {
            MusicObject::MusicSongAttribute attr;
            attr.m_bitrate = MB_96;
            attr.m_format = WMA_FILE_PREFIX;
            attr.m_size = "-";
            attr.m_url = QString("http://%1/resource/%2").arg(mp3Url).arg(v);
            info->m_songAttrs.append(attr);
        }
    }

    QString aacUrl = readXmlTextByTagName("aacdl");
    if(!aacUrl.isEmpty())
    {
        QString v = readXmlTextByTagName("aacpath");
        if(!v.isEmpty())
        {
            MusicObject::MusicSongAttribute attr;
            attr.m_bitrate = MB_32;
            attr.m_format = AAC_FILE_PREFIX;
            attr.m_size = "-";
            attr.m_url = QString("http://%1/resource/%2").arg(aacUrl).arg(v);
            info->m_songAttrs.append(attr);
        }
    }
}



MusicDownLoadQueryKWThread::MusicDownLoadQueryKWThread(QObject *parent)
    : MusicDownLoadQueryThreadAbstract(parent)
{
    m_queryServer = "Kuwo";
    m_pageSize = 40;
}

QString MusicDownLoadQueryKWThread::getClassName()
{
    return staticMetaObject.className();
}

void MusicDownLoadQueryKWThread::startToSearch(QueryType type, const QString &text)
{
    if(!m_manager)
    {
        return;
    }

    M_LOGGER_INFO(QString("%1 startToSearch %2").arg(getClassName()).arg(text));
    m_currentType = type;
    m_searchText = text.trimmed();

    emit clearAllItems();
    m_musicSongInfos.clear();

    startToPage(0);
}

void MusicDownLoadQueryKWThread::startToPage(int offset)
{
    if(!m_manager)
    {
        return;
    }

    M_LOGGER_INFO(QString("%1 startToPage %2").arg(getClassName()).arg(offset));
    deleteAll();

    QUrl musicUrl = MusicUtils::Algorithm::mdII(KW_SONG_SEARCH_URL, false)
                    .arg(m_searchText).arg(offset).arg(m_pageSize);
    m_interrupt = true;
    m_pageTotal = 0;
    m_pageIndex = offset;

    QNetworkRequest request;
    request.setUrl(musicUrl);
    request.setRawHeader("Content-Type", "application/x-www-form-urlencoded");
    request.setRawHeader("User-Agent", MusicUtils::Algorithm::mdII(KW_UA_URL_1, ALG_UA_KEY, false).toUtf8());
    setSslConfiguration(&request);

    m_reply = m_manager->get(request);
    connect(m_reply, SIGNAL(finished()), SLOT(downLoadFinished()));
    connect(m_reply, SIGNAL(error(QNetworkReply::NetworkError)), SLOT(replyError(QNetworkReply::NetworkError)));
}

void MusicDownLoadQueryKWThread::startToSingleSearch(const QString &text)
{
    if(!m_manager)
    {
        return;
    }

    M_LOGGER_INFO(QString("%1 startToSingleSearch %2").arg(getClassName()).arg(text));
    m_interrupt = true;
    QUrl musicUrl = MusicUtils::Algorithm::mdII(KW_SONG_INFO_URL, false).arg(text);
    deleteAll();

    QNetworkRequest request;
    request.setUrl(musicUrl);
    request.setRawHeader("Content-Type", "application/x-www-form-urlencoded");
    request.setRawHeader("User-Agent", MusicUtils::Algorithm::mdII(KW_UA_URL_1, ALG_UA_KEY, false).toUtf8());
    setSslConfiguration(&request);

    QNetworkReply *reply = m_manager->get(request);
    connect(reply, SIGNAL(finished()), SLOT(singleDownLoadFinished()));
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), SLOT(replyError(QNetworkReply::NetworkError)));
}

void MusicDownLoadQueryKWThread::downLoadFinished()
{
    if(!m_reply || !m_manager)
    {
        deleteAll();
        return;
    }

    M_LOGGER_INFO(QString("%1 downLoadFinished").arg(getClassName()));
    m_interrupt = false;

    if(m_reply->error() == QNetworkReply::NoError)
    {
        QByteArray bytes = m_reply->readAll();///Get all the data obtained by request

        QJson::Parser parser;
        bool ok;
        QVariant data = parser.parse(bytes.replace("'", "\""), &ok);

        if(ok)
        {
            QVariantMap value = data.toMap();
            if(value.contains("abslist"))
            {
                m_pageTotal = value["TOTAL"].toInt();
                QVariantList datas = value["abslist"].toList();
                foreach(const QVariant &var, datas)
                {
                    if(var.isNull())
                    {
                        continue;
                    }

                    value = var.toMap();
                    MusicObject::MusicSongInformation musicInfo;
                    musicInfo.m_singerName = value["ARTIST"].toString();
                    musicInfo.m_songName = value["SONGNAME"].toString();
                    musicInfo.m_timeLength = MusicTime::msecTime2LabelJustified(value["DURATION"].toInt()*1000);

                    musicInfo.m_songId = value["MUSICRID"].toString().replace("MUSIC_", "");
                    musicInfo.m_artistId = value["ARTISTID"].toString();
                    musicInfo.m_albumId = value["ALBUMID"].toString();

                    if(!m_querySimplify)
                    {
                        musicInfo.m_albumName = value["ALBUM"].toString();

                        if(m_interrupt || !m_manager || m_stateCode != MusicNetworkAbstract::Init) return;
                        readFromMusicSongPic(&musicInfo);
                        if(m_interrupt || !m_manager || m_stateCode != MusicNetworkAbstract::Init) return;
                        musicInfo.m_lrcUrl = MusicUtils::Algorithm::mdII(KW_SONG_LRC_URL, false).arg(musicInfo.m_songId);
                        ///music normal songs urls
                        readFromMusicSongAttribute(&musicInfo, value["FORMATS"].toString(), m_searchQuality, m_queryAllRecords);
                        if(m_interrupt || !m_manager || m_stateCode != MusicNetworkAbstract::Init) return;

                        if(musicInfo.m_songAttrs.isEmpty())
                        {
                            continue;
                        }
                        ////////////////////////////////////////////////////////////
                        if(!findUrlFileSize(&musicInfo.m_songAttrs)) return;
                        ////////////////////////////////////////////////////////////
                        MusicSearchedItem item;
                        item.m_songName = musicInfo.m_songName;
                        item.m_singerName = musicInfo.m_singerName;
                        item.m_albumName = musicInfo.m_albumName;
                        item.m_time = musicInfo.m_timeLength;
                        item.m_type = mapQueryServerString();
                        emit createSearchedItem(item);
                    }
                    m_musicSongInfos << musicInfo;
                }
            }
        }
    }

    emit downLoadDataChanged(QString());
    deleteAll();
    M_LOGGER_INFO(QString("%1 downLoadFinished deleteAll").arg(getClassName()));
}

void MusicDownLoadQueryKWThread::singleDownLoadFinished()
{
    QNetworkReply *reply = MObject_cast(QNetworkReply*, QObject::sender());

    M_LOGGER_INFO(QString("%1 singleDownLoadFinished").arg(getClassName()));
    emit clearAllItems();
    m_musicSongInfos.clear();
    m_interrupt = false;

    if(reply && m_manager &&reply->error() == QNetworkReply::NoError)
    {
        QByteArray data = reply->readAll();
        data.insert(0, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n");
        data.replace("&", "%26");

        MusicObject::MusicSongInformation musicInfo;
        MusicKWMusicInfoConfigManager xml;
        if(xml.fromByteArray(data))
        {
            xml.readMusicInfoConfig(&musicInfo);

            if(m_interrupt || !m_manager || m_stateCode != MusicNetworkAbstract::Init) return;
            readFromMusicSongPic(&musicInfo);
            if(m_interrupt || !m_manager || m_stateCode != MusicNetworkAbstract::Init) return;
            musicInfo.m_lrcUrl = MusicUtils::Algorithm::mdII(KW_SONG_LRC_URL, false).arg(musicInfo.m_songId);
            ////////////////////////////////////////////////////////////
            if(!findUrlFileSize(&musicInfo.m_songAttrs)) return;
            ////////////////////////////////////////////////////////////
            if(!musicInfo.m_songAttrs.isEmpty())
            {
                MusicSearchedItem item;
                item.m_songName = musicInfo.m_songName;
                item.m_singerName = musicInfo.m_singerName;
                item.m_time = musicInfo.m_timeLength;
                item.m_type = mapQueryServerString();
                emit createSearchedItem(item);
                m_musicSongInfos << musicInfo;
            }
        }
    }

    emit downLoadDataChanged(QString());
    deleteAll();
    M_LOGGER_INFO(QString("%1 singleDownLoadFinished deleteAll").arg(getClassName()));
}
