#ifndef DBConfForWeb_H
#define DBConfForWeb_H

#include "core.hpp"
#include <string>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/foreach.hpp>

// xml source file
#define OPTXMLSRCFILE          "optlist.xml"
#define OPTOTHERINFOFORWEBFILE "optOtherInfoForWeb.xml"

// output file path
// #define DBCONFFORWEBPATH "../../doc/administration/database/topics/runtime_configuration"
#define DBCONFFORWEBPATH "../../doc/bak/runtime_configuration"
#define FILESUFFIX ".dita"

class OptEle
{
public:
    std::string longtag ;
    std::string shorttag ;
    std::string typeofwebtag ;
    std::string detailtag ;
    BOOLEAN hiddentag ;
    OptEle()
    {
       longtag = "--" ;
       shorttag = "-" ;
       hiddentag = FALSE ;
    }
} ;

class OptOtherInfoEle
{
public:
    std::string titletag ;
    std::string subtitletag ;
    // stentry tags
    std::string stentry_nametag ;
    std::string stentry_acronymtag ;
    std::string stentry_typetag ;
    std::string stentry_desttag ;
    // note tags
    std::string firsttag ;
    std::string secondtag ;
} ;

class OptGenForWeb
{
    const char *language ;
    std::vector<OptOtherInfoEle*> optOtherInfo ;
    std::vector<OptEle*> optlist ;
    void loadOtherInfoFromXML () ;
    void loadFromXML () ;
    std::string genOptions () ;
    void gendoc () ;

public:
    OptGenForWeb ( const char* lang ) ;
    ~OptGenForWeb () ;
    void run () ;
};

#endif
