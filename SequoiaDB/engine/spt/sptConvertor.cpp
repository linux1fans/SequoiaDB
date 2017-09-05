/*******************************************************************************


   Copyright (C) 2011-2014 SequoiaDB Ltd.

   This program is free software: you can redistribute it and/or modify
   it under the term of the GNU Affero General Public License, version 3,
   as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warrenty of
   MARCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program. If not, see <http://www.gnu.org/license/>.

   Source File Name = sptConvertor.cpp

   Descriptive Name =

   When/how to use: this program may be used on binary and text-formatted
   versions of Script component. This file contains structures for javascript
   engine wrapper

   Dependencies: N/A

   Restrictions: N/A

   Change Activity:
   defect Date        Who Description
   ====== =========== === ==============================================
          01/13/2013  YW Initial Draft

   Last Changed =

*******************************************************************************/

#include "ossUtil.hpp"
#include "sptConvertor.hpp"
#include "pd.hpp"
#include "ossMem.hpp"
#include "utilStr.hpp"
#include "../client/base64c.h"
#include "../client/jstobs.h"
#include <boost/lexical_cast.hpp>

#define SPT_CONVERTOR_SPE_OBJSTART '$'
#define SPT_SPEOBJ_MINKEY "$minKey"
#define SPT_SPEOBJ_MAXKEY "$maxKey"
#define SPT_SPEOBJ_TIMESTAMP "$timestamp"
#define SPT_SPEOBJ_DATE "$date"
#define SPT_SPEOBJ_REGEX "$regex"
#define SPT_SPEOBJ_OPTION "$options"
#define SPT_SPEOBJ_BINARY "$binary"
#define SPT_SPEOBJ_TYPE "$type"
#define SPT_SPEOBJ_OID "$oid"

/*
// check date type bounds
#define SDB_DATE_TYPE_CHECK_BOUND(tm)                 \
   do {                                               \
      if( (INT64)tm/1000 < TIME_STAMP_DATE_MIN ||     \
          (INT64)tm/1000 > TIME_STAMP_DATE_MAX ) {    \
         rc = SDB_INVALIDARG ;                        \
         goto error ;                                 \
      }                                               \
   } while( 0 )
*/    
// check timestamp type bounds
#define SDB_TIMESTAMP_TYPE_CHECK_BOUND(tm)            \
   do {                                               \
      if ( (INT64)tm < TIME_STAMP_TIMESTAMP_MIN ||    \
           (INT64)tm > TIME_STAMP_TIMESTAMP_MAX ) {   \
           rc = SDB_INVALIDARG ;                      \
           goto error ;                               \
      }                                               \
   } while( 0 )

extern JSBool is_objectid( JSContext *, JSObject * ) ;
extern JSBool is_bindata( JSContext *, JSObject * ) ;
extern JSBool is_jsontypes( JSContext *, JSObject * ) ;
extern JSBool is_timestamp( JSContext *, JSObject * ) ;
extern JSBool is_regex( JSContext *, JSObject * ) ;
extern JSBool is_minkey( JSContext *, JSObject * ) ;
extern JSBool is_maxkey( JSContext *, JSObject * ) ;
extern JSBool is_numberlong( JSContext *, JSObject * ) ;
extern JSBool is_sdbdate( JSContext *, JSObject * ) ;

INT32 sptConvertor::toBson( JSObject *obj , bson **bs )
{
   INT32 rc = SDB_OK ;
   SDB_ASSERT( NULL != _cx && NULL != bs, "can not be NULL" ) ;

   /// can not use SDB_OSS_MALLOC
   *bs = bson_create() ;
   if ( NULL == *bs )
   {
      rc = SDB_OOM ;
      goto error ;
   }
   bson_init( *bs ) ;

   rc = _traverse( obj, *bs ) ;
   if ( SDB_OK != rc )
   {
      goto error ;
   }

   rc = bson_finish( *bs ) ;
   if ( rc )
   {
      rc = SDB_INVALIDARG ;
      goto error ;
   }

done:
   return rc ;
error:
   if ( NULL != *bs )
   {
      bson_dispose( *bs ) ;
      *bs = NULL ;
   }
   goto done ;
}

INT32 sptConvertor::toBson( JSObject *obj, bson *bs )
{
   INT32 rc = SDB_OK ;
   SDB_ASSERT( NULL != obj && NULL != bs, "can not be NULL" ) ;
   rc = _traverse( obj, bs ) ;
   if ( SDB_OK != rc )
   {
      goto error ;
   }

   rc = bson_finish( bs ) ;
   if ( rc )
   {
      rc = SDB_INVALIDARG ;
      goto error ;
   }
done:
   return rc ;
error:
   goto done ;
}

INT32 sptConvertor::_traverse( JSObject *obj , bson *bs )
{
   INT32 rc = SDB_OK ;
   JSIdArray *properties = NULL ;
   if ( NULL == obj )
   {
      goto done ;
   }

   properties = JS_Enumerate( _cx, obj ) ;
   if ( NULL == properties )
   {
      rc = SDB_INVALIDARG ;
      goto error ;
   }

   for ( jsint i = 0; i < properties->length; i++ )
   {
      jsid id = properties->vector[i] ;
      jsval fieldName, fieldValue ;
      std::string name ;
      if ( !JS_IdToValue( _cx, id, &fieldName ))
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      rc = _toString( fieldName, name ) ;
      if ( SDB_OK != rc )
      {
         goto error ;
      }

      if ( !JS_GetProperty( _cx, obj, name.c_str(), &fieldValue ))
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      rc = _appendToBson( name, fieldValue, bs ) ;
      if ( SDB_OK != rc )
      {
         goto error ;
      }
   }
done:
   return rc ;
error:
   goto done ;
}

INT32 sptConvertor::_addObjectId( JSObject *obj,
                                  const CHAR *key,
                                  bson *bs )
{
   INT32 rc = SDB_OK ;
   std::string strValue ;
   jsval value ;
   if ( !_getProperty( obj, "_str", JSTYPE_STRING, value ))
   {
      rc = SDB_SYS ;
      goto error ;
   }

   rc = _toString( value, strValue ) ;
   if ( SDB_OK != rc )
   {
      goto error ;
   }

   if ( 24 != strValue.length() )
   {
      rc = SDB_SYS ;
      goto error ;
   }

   bson_oid_t oid ;
   bson_oid_from_string( &oid, strValue.c_str() ) ;
   bson_append_oid( bs, key, &oid ) ;
done:
   return rc ;
error:
   goto done ;
}

INT32 sptConvertor::_addBinData( JSObject *obj,
                                 const CHAR *key,
                                 bson *bs )
{
   INT32 rc = SDB_OK ;
   std::string typeName ;
   std::string strBin, strType ;
   jsval jsBin, jsType ;
   CHAR *decode = NULL ;
   INT32 decodeSize = 0 ;
   UINT32 binType = 0 ;

   if ( !_getProperty( obj, "_data",
                       JSTYPE_STRING, jsBin ))
   {
      rc = SDB_SYS ;
      goto error ;
   }

   if ( !_getProperty( obj, "_type",
                       JSTYPE_STRING, jsType ))
   {
      rc = SDB_SYS ;
      goto error ;
   }

   rc = _toString( jsBin, strBin ) ;
   if ( SDB_OK != rc )
   {
      goto error ;
   }

   rc = _toString( jsType, strType ) ;
   if ( SDB_OK != rc || strType.empty())
   {
      goto error ;
   }

   try
   {
      binType = boost::lexical_cast<INT32>( strType.c_str() ) ;
      if ( binType > 255 )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }
   }
   catch ( std::bad_cast &e )
   {
      PD_LOG( PDERROR, "bad type for binary:%s", strType.c_str() ) ;
      rc = SDB_INVALIDARG ;
      goto error ;
   }

   decodeSize = getDeBase64Size( strBin.c_str() ) ;
   if ( decodeSize < 0 )
   {
      PD_LOG( PDERROR, "invalid bindata %s", strBin.c_str() ) ;
      rc = SDB_INVALIDARG ;
      goto error ;
   }
   if( decodeSize > 0 )
   {
      decode = ( CHAR * )SDB_OSS_MALLOC( decodeSize ) ;
      if ( NULL == decode )
      {
         PD_LOG( PDERROR, "failed to allocate mem." ) ;
         rc = SDB_OOM ;
         goto error ;
      }
      memset ( decode, 0, decodeSize ) ;
      if ( base64Decode( strBin.c_str(), decode, decodeSize ) < 0 )
      {
         PD_LOG( PDERROR, "failed to decode base64 code" ) ;
         rc = SDB_INVALIDARG ;
         SDB_OSS_FREE( decode ) ;
         goto error ;
      }
      /// we can not push '\0' to bson
      bson_append_binary( bs, key, binType,
                          decode, decodeSize - 1 ) ;
   }
   else
   {
      bson_append_binary( bs, key, binType,
                          "", 0 ) ;
   }
done:
   SDB_OSS_FREE( decode ) ;
   return rc ;
error:
   goto done ;
}

INT32 sptConvertor::_addTimestamp( JSObject *obj,
                                   const CHAR *key,
                                   bson *bs )
{
   INT32 rc = SDB_OK ;
   std::string strValue ;
   jsval value ;
   time_t tm ;
   UINT64 usec = 0 ;
   bson_timestamp_t btm ;
   if ( !_getProperty( obj, "_t", JSTYPE_STRING, value ))
   {
      rc = SDB_SYS ;
      goto error ;
   }

   rc = _toString( value, strValue ) ;
   if ( SDB_OK != rc )
   {
      goto error ;
   }

   rc = engine::utilStr2TimeT( strValue.c_str(),
                               tm,
                               &usec ) ;
   if ( SDB_OK != rc )
   {
      goto error ;
   }

   // check bounds
   SDB_TIMESTAMP_TYPE_CHECK_BOUND( tm ) ;
   // append to bson
   btm.t = tm;
   btm.i = usec ;
   bson_append_timestamp( bs, key, &btm ) ;
done:
   return rc ;
error:
   goto done ;
}

INT32 sptConvertor::_addRegex( JSObject *obj,
                               const CHAR *key,
                               bson *bs )
{
   INT32 rc = SDB_OK ;
   std::string optionName ;
   std::string strRegex, strOption ;
   jsval jsRegex, jsOption ;

   if ( !_getProperty( obj, "_regex",
                       JSTYPE_STRING, jsRegex ))
   {
      rc = SDB_SYS ;
      goto error ;
   }

   if ( !_getProperty( obj, "_option",
                       JSTYPE_STRING, jsOption ))
   {
      rc = SDB_SYS ;
      goto error ;
   }

   rc = _toString( jsRegex, strRegex ) ;
   if ( SDB_OK != rc )
   {
      goto error ;
   }

   rc = _toString( jsOption, strOption ) ;
   if ( SDB_OK != rc )
   {
      goto error ;
   }

   bson_append_regex( bs, key, strRegex.c_str(), strOption.c_str() ) ;
done:
   return rc ;
error:
   goto done ;
}

INT32 sptConvertor::_addMinKey( JSObject *obj,
                                const CHAR *key,
                                bson *bs )
{
   bson_append_minkey( bs, key ) ;
   return SDB_OK ;
}

INT32 sptConvertor::_addMaxKey( JSObject *obj,
                                const CHAR *key,
                                bson *bs )
{
   bson_append_maxkey( bs, key ) ;
   return SDB_OK ;
}

INT32 sptConvertor::_addNumberLong( JSObject *obj,
                                    const CHAR *key,
                                    bson *bs )
{
   INT32 rc = SDB_OK ;
   jsval jsV = JSVAL_VOID ;
   FLOAT64 fv = 0 ;
   INT64 n = 0 ;
   string strv ;

   if ( !_getProperty( obj, "_v",
                       JSTYPE_NUMBER, jsV ))
   {
      if ( !_getProperty( obj, "_v",
                          JSTYPE_STRING, jsV ) )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      rc = _toString( jsV, strv ) ;
      if ( SDB_OK != rc )
      {
         goto error ;
      }

      try
      {
         n = boost::lexical_cast<INT64>( strv ) ;
      }
      catch ( std::bad_cast &e )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }
   }
   else
   {
      rc = _toDouble( jsV, fv ) ;
      if ( SDB_OK != rc )
      {
         goto error ;
      }
      n = fv ;
   }

   bson_append_long( bs, key, n ) ;
done:
   return rc ;
error:
   goto done ;
}

INT32 sptConvertor::_addSdbDate( JSObject *obj,
                                 const CHAR *key,
                                 bson *bs )
{
   INT32 rc = SDB_OK ;
   std::string strValue ;
   jsval value ;
   UINT64 tm = 0 ;
   bson_date_t datet ;
   if ( _getProperty( obj, "_d", JSTYPE_STRING, value ) )
   {
      rc = _toString( value, strValue ) ;
      if ( SDB_OK != rc )
      {
         goto error ;
      }

      rc = engine::utilStr2Date( strValue.c_str(), tm ) ;
      if ( SDB_OK != rc )
      {  // maybe the format is {dateKey:SdbDate("-30610252800000")}
         try
         {
            tm = boost::lexical_cast<UINT64>( strValue.c_str() ) ;
         }
         catch( boost::bad_lexical_cast &e )
         {
            rc = SDB_INVALIDARG ;
            goto error ;
         }
      }
   }
   else if (  _getProperty( obj, "_d", JSTYPE_NUMBER, value ) )
   {
      FLOAT64 fv = 0 ;
      rc = _toDouble( value, fv ) ;
      if ( SDB_OK != rc )
      {
         goto error ;
      }
      tm = fv ;
   }
   else
   {
      rc = SDB_INVALIDARG ;
      goto error ;
   }

   // append to bson
   datet = tm ;
   bson_append_date( bs, key, datet ) ;
done:
   return rc ;
error:
   goto done ;
}

INT32 sptConvertor::_addJsonTypes( JSObject *obj,
                                   const CHAR *key,
                                   bson *bs )
{
   INT32 rc = SDB_OK ;
   if ( is_objectid( _cx, obj ) )
   {
      rc = _addObjectId( obj, key, bs ) ;
   }
   else if ( is_bindata( _cx, obj ) )
   {
      rc = _addBinData( obj, key, bs ) ;
   }
   else if ( is_timestamp( _cx, obj ) )
   {
      rc = _addTimestamp( obj, key, bs ) ;
   }
   else if ( is_regex( _cx, obj ) )
   {
      rc = _addRegex( obj, key, bs ) ;
   }
   else if ( is_minkey( _cx, obj ) )
   {
      rc = _addMinKey( obj, key, bs ) ;
   }
   else if ( is_maxkey( _cx, obj ) )
   {
      rc = _addMaxKey( obj, key, bs ) ;
   }
   else if ( is_numberlong( _cx, obj ) )
   {
      rc = _addNumberLong( obj, key, bs ) ; 
   }
   else if ( is_sdbdate( _cx, obj ) )
   {
      rc = _addSdbDate( obj, key, bs ) ;
   }
   else
   {
      rc = SDB_INVALIDARG ;
   }

   if ( SDB_OK != rc )
   {
      goto error ;
   }
done:
   return rc ;
error:
   goto done ; 
}

INT32 sptConvertor::_addSpecialObj( JSObject *obj,
                                    const CHAR *key,
                                    bson *bs )
{
   //BOOLEAN ret = TRUE ;
   INT32 rc = SDB_OK ;
   JSIdArray *properties = JS_Enumerate( _cx, obj ) ;
   if ( NULL == properties || 0 == properties->length )
   {
      rc = SDB_SPT_NOT_SPECIAL_JSON ;
      goto error ;
   }

   {
   /// get the first ele
   jsid id = properties->vector[0] ;
   jsval fieldName ;
   std::string name ;
   if ( !JS_IdToValue( _cx, id, &fieldName ))
   {
      rc = SDB_SYS ;
      goto error ;
   }

   rc = _toString( fieldName, name ) ;
   if ( SDB_OK != rc )
   {
      rc = SDB_SYS ;
      goto error ;
   }

   if ( name.length() <= 1 )
   {
      rc = SDB_SPT_NOT_SPECIAL_JSON ;
      goto error ;
   }

   /// start with '$'
   if ( SPT_CONVERTOR_SPE_OBJSTART != name.at(0) )
   {
      rc = SDB_SPT_NOT_SPECIAL_JSON ;
      goto error ;
   }

   if ( 0 == name.compare( SPT_SPEOBJ_MINKEY ) &&
        1 == properties->length )
   {
      jsval value ;
      if ( !_getProperty( obj, name.c_str(), JSTYPE_NUMBER, value ) )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      bson_append_minkey( bs, key ) ;
   }
   else if ( 0 == name.compare(SPT_SPEOBJ_MAXKEY) &&
             1 == properties->length )
   {
      jsval value ;
      if ( !_getProperty( obj, name.c_str(), JSTYPE_NUMBER, value ) )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      bson_append_maxkey( bs, key ) ;
   }
   else if ( 0 == name.compare( SPT_SPEOBJ_OID ) &&
             1 == properties->length )
   {
      std::string strValue ;
      jsval value ;
      if ( !_getProperty( obj, name.c_str(), JSTYPE_STRING, value ))
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      rc = _toString( value, strValue ) ;
      if ( SDB_OK != rc )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      if ( 24 != strValue.length() || !_isValidOid( strValue.c_str() ) )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      bson_oid_t oid ;
      bson_oid_from_string( &oid, strValue.c_str() ) ;
      bson_append_oid( bs, key, &oid ) ;
   }
   else if ( 0 == name.compare( SPT_SPEOBJ_TIMESTAMP ) &&
             1 == properties->length )
   {
      std::string strValue ;
      jsval value ;
      time_t tm ;
      UINT64 usec = 0 ;
      bson_timestamp_t btm ;
      if ( !_getProperty( obj, name.c_str(), JSTYPE_STRING, value ))
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      rc = _toString( value, strValue ) ;
      if ( SDB_OK != rc )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      if ( SDB_OK != engine::utilStr2TimeT( strValue.c_str(),
                                            tm,
                                            &usec ))
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }
      // check bounds
      SDB_TIMESTAMP_TYPE_CHECK_BOUND( tm ) ;
      // append timestamp
      btm.t = tm;
      btm.i = usec ;
      bson_append_timestamp( bs, key, &btm ) ;
   }
   else if ( 0 == name.compare( SPT_SPEOBJ_DATE ) &&
             1 == properties->length )
   {
      std::string strValue ;
      jsval value ;
      UINT64 tm ;
      bson_date_t datet ;
      if ( TRUE == _getProperty( obj, name.c_str(), JSTYPE_STRING, value ) )
      {
         // the format is {$date:"2000-01-01"} or
         // {$date:"2000-01-01T(t)01:30:24:999999Z(z)"} or
         // {$date:"2000-01-01T(t)01:30:24:000000+0800"}
         rc = _toString( value, strValue ) ;
         if ( SDB_OK != rc )
         {
            rc = SDB_INVALIDARG ;
            goto error ;
         }
         rc = engine::utilStr2Date( strValue.c_str(), tm ) ;
         if ( SDB_OK != rc )
         {  
            // maybe the format is {$date:"253402185600000"}
            try
            {
               tm = boost::lexical_cast<UINT64>( strValue.c_str() ) ;
            }
            catch( boost::bad_lexical_cast &e )
            {
               rc = SDB_INVALIDARG ;
               goto error ;
            }
         }
      }
      else if ( TRUE == _getProperty( obj, name.c_str(), JSTYPE_OBJECT, value ) )
      {
         // the format is {$date:{$numberLong:"946656000000"}}
         JSObject *tmpObj = JSVAL_TO_OBJECT( value ) ;
         jsval tmpValue ;
         if ( NULL == tmpObj )
         {
            rc = SDB_INVALIDARG ;
            goto error ;
         }
         if ( TRUE == _getProperty( tmpObj, "$numberLong",
                                    JSTYPE_STRING, tmpValue ) )
         {
            rc = _toString( tmpValue, strValue ) ;
            if ( SDB_OK != rc )
            {
               rc = SDB_INVALIDARG ;
               goto error ;
            }
            try
            {
               tm = boost::lexical_cast<UINT64>( strValue ) ;
            }
            catch( boost::bad_lexical_cast &e )
            {
               rc = SDB_INVALIDARG ;
               goto error ;
            }
         }
         else if ( TRUE == _getProperty( tmpObj, "$numberLong",
                                         JSTYPE_NUMBER, tmpValue ) )
         {
            // the format is {$date:{$numberLong:946656000000}}
            FLOAT64 fv = 0 ;
            rc = _toDouble( tmpValue, fv ) ;
            if ( SDB_OK != rc )
            {
               goto error ;
            }
            tm = fv ;
         }
         else
         {
            rc = SDB_INVALIDARG ;
            goto error ;
         }
      }
      else if ( TRUE == _getProperty( obj, name.c_str(), JSTYPE_NUMBER, value ) )
      {
         // the format is {$date:946656000000}
         FLOAT64 fv = 0 ;
         rc = _toDouble( value, fv ) ;
         if ( SDB_OK != rc )
         {
            goto error ;
         }
         tm = fv ;
      }
      else
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      // append date      
      datet = tm ;
      rc = bson_append_date( bs, key, datet ) ;
      if ( SDB_OK !=rc )
      {
         rc = SDB_DRIVER_BSON_ERROR ;
         goto  error ;
      }
   }
   else if ( 0 == name.compare( SPT_SPEOBJ_REGEX ) &&
             2 == properties->length )
   {
      std::string optionName ;
      std::string strRegex, strOption ;
      jsval jsRegex, jsOption ;
      jsid optionid = properties->vector[1] ;
      jsval optionValName ;

      if ( !JS_IdToValue( _cx, optionid, &optionValName ))
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      rc = _toString( optionValName, optionName ) ;
      if ( SDB_OK != rc )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      if ( 0 != optionName.compare( SPT_SPEOBJ_OPTION ) )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      if ( !_getProperty( obj, name.c_str(),
                          JSTYPE_STRING, jsRegex ))
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      if ( !_getProperty( obj, optionName.c_str(),
                          JSTYPE_STRING, jsOption ))
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      rc = _toString( jsRegex, strRegex ) ;
      if ( SDB_OK != rc )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      rc = _toString( jsOption, strOption ) ;
      if ( SDB_OK != rc )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      bson_append_regex( bs, key, strRegex.c_str(), strOption.c_str() ) ;
   }
   else if ( 0 == name.compare( SPT_SPEOBJ_BINARY ) &&
             2 == properties->length )
   {
      std::string typeName ;
      std::string strBin, strType ;
      jsval jsBin, jsType ;
      jsid typeId = properties->vector[1] ;
      jsval typeValName ;
      CHAR *decode = NULL ;
      INT32 decodeSize = 0 ;
      UINT32 binType = 0 ;

      if ( !JS_IdToValue( _cx, typeId, &typeValName ))
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      rc = _toString( typeValName, typeName ) ;
      if ( SDB_OK != rc )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      if ( 0 != typeName.compare( SPT_SPEOBJ_TYPE ) )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      if ( !_getProperty( obj, name.c_str(),
                          JSTYPE_STRING, jsBin ))
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      if ( !_getProperty( obj, typeName.c_str(),
                          JSTYPE_STRING, jsType ))
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      rc = _toString( jsBin, strBin ) ;
      if ( SDB_OK != rc )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      rc = _toString( jsType, strType ) ;
      if ( SDB_OK != rc || strType.empty())
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      try
      {
         binType = boost::lexical_cast<INT32>( strType.c_str() ) ;
         if ( binType > 255 )
         {
            rc = SDB_INVALIDARG ;
            goto error ;
         }
      }
      catch ( std::bad_cast &e )
      {
         PD_LOG( PDERROR, "bad type for binary:%s", strType.c_str() ) ;
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      decodeSize = getDeBase64Size( strBin.c_str() ) ;
      if ( decodeSize < 0 )
      {
         PD_LOG( PDERROR, "invalid decode %s", strBin.c_str() ) ;
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      if( decodeSize > 0 )
      {
         decode = ( CHAR * )SDB_OSS_MALLOC( decodeSize ) ;
         if ( NULL == decode )
         {
            PD_LOG( PDERROR, "failed to allocate mem." ) ;
            rc = SDB_OOM ;
            goto error ;
         }
         if ( base64Decode( strBin.c_str(), decode, decodeSize ) < 0 )
         {
            PD_LOG( PDERROR, "failed to decode base64 code" ) ;
            rc = SDB_INVALIDARG ;
            SDB_OSS_FREE( decode ) ;
            goto error ;
         }
   
         /// we can not push '\0' to bson
         bson_append_binary( bs, key, binType,
                             decode, decodeSize - 1 ) ;
         SDB_OSS_FREE( decode ) ;
      }
      else
      {
         bson_append_binary( bs, key, binType,
                             "", 0 ) ;
      }
   }
   else
   {
      rc = SDB_SPT_NOT_SPECIAL_JSON ;
      goto error ;
   }
   }

done:
   return rc ;
error:
   //ret = FALSE ;
   goto done ;
}

INT32 sptConvertor::_appendToBson( const std::string &name,
                                   const jsval &val,
                                   bson *bs )
{
   INT32 rc = SDB_OK ;
   switch (JS_TypeOfValue( _cx, val ))
   {
      case JSTYPE_VOID :
      {
         bson_append_undefined( bs, name.c_str() ) ;
         break ;
      }
      case JSTYPE_NULL :
      {
         bson_append_null( bs, name.c_str() ) ;
         break ;
      }
      case JSTYPE_NUMBER :
      {
         if ( JSVAL_IS_INT( val ) )
         {
            INT32 iN = 0 ;
            rc = _toInt( val, iN ) ;
            if ( SDB_OK != rc )
            {
               goto error ;
            }
            bson_append_int( bs, name.c_str(), iN ) ;
         }
         else
         {
            FLOAT64 fV = 0 ;
            rc = _toDouble( val, fV ) ;
            if ( SDB_OK != rc )
            {
               goto error ;
            }
            bson_append_double( bs, name.c_str(), fV ) ;
         }
         break ;
      }
      case JSTYPE_STRING :
      {
         std::string str ;
         rc = _toString( val, str ) ;
         if ( SDB_OK != rc )
         {
            goto error ;
         }
         bson_append_string( bs, name.c_str(), str.c_str() ) ;
         break ;
      }
      case JSTYPE_BOOLEAN :
      {
         BOOLEAN bL = TRUE ;
         rc = _toBoolean( val, bL ) ;
         if ( SDB_OK != rc )
         {
            goto error ;
         }
         bson_append_bool( bs, name.c_str(), bL ) ;
         break ;
      }
      case JSTYPE_OBJECT :
      {
         if ( JSVAL_IS_NULL( val ) )
         {
            bson_append_null( bs, name.c_str() ) ;
         }
         else
         {
            JSObject *obj = JSVAL_TO_OBJECT( val ) ;
            if ( NULL == obj )
            {
               bson_append_null( bs, name.c_str() ) ;
            }
            else if( is_jsontypes( _cx, obj ) )
            {
               rc = _addJsonTypes( obj, name.c_str(), bs ) ;
               if ( SDB_OK != rc )
               {
                  goto error ;
               }
            }
            else
            {
               rc = _addSpecialObj( obj, name.c_str(), bs ) ;
               if ( SDB_SPT_NOT_SPECIAL_JSON == rc )
               {
                  bson *bsobj = NULL ;
                  rc = toBson( obj, &bsobj ) ;
                  if ( SDB_OK != rc )
                  {
                     goto error ;
                  }

                  if ( JS_IsArrayObject( _cx, obj ) )
                  {
                     bson_append_array( bs, name.c_str(), bsobj ) ;
                  }
                  else
                  {
                     bson_append_bson( bs, name.c_str(), bsobj ) ;
                  }

                  bson_destroy( bsobj ) ;
               }
               else if ( SDB_OK != rc )
               {
                  goto error ;
               }
               else
               {
                  /// do nothing.
               }
            }
         }
         break ;
      }
      case JSTYPE_FUNCTION :
      {
         std::string str ;
         rc = _toString( val, str ) ;
         if ( SDB_OK != rc )
         {
            goto error ;
         }

         bson_append_code( bs, name.c_str(), str.c_str() ) ;
         break ;
      }
      default :
      {
         SDB_ASSERT( FALSE, "unexpected type" ) ;
         rc = SDB_INVALIDARG ;
         goto error ;
      }
   }
done:
   return rc ;
error:
   goto done ;
}

BOOLEAN sptConvertor::_getProperty( JSObject *obj,
                                    const CHAR *name,
                                    JSType type,
                                    jsval &val )
{
   if ( !JS_GetProperty( _cx, obj, name, &val ) )
   {
      return FALSE ;
   }
   else if ( type != JS_TypeOfValue( _cx, val ) )
   {
      return FALSE ;
   }
   else
   {
      return TRUE ;
   }
}

INT32 sptConvertor::toString( JSContext *cx,
                              const jsval &val,
                              std::string &str )
{
   INT32 rc = SDB_OK ;
   //CHAR *utf8 = NULL ;
   SDB_ASSERT( NULL != cx, "impossible" ) ;
   size_t len = 0 ;
   JSString *jsStr = JS_ValueToString( cx, val ) ;
   if ( NULL == jsStr )
   {
      goto done ;
   }
   len = JS_GetStringLength( jsStr ) ;
   if ( 0 == len )
   {
      goto done ;
   }
   else
   {
/*      size_t cLen = len * 6 + 1 ;
      const jschar *utf16 = JS_GetStringCharsZ( cx, jsStr ) ; ;
      utf8 = (CHAR *)SDB_OSS_MALLOC( cLen ) ;
      if ( NULL == utf8 )
      {
         rc = SDB_OOM ;
         goto error ;
      }
      if ( !JS_EncodeCharacters( cx, utf16, len, utf8, &cLen ) )
      {
         rc = SDB_INVALIDARG ;
         goto error ;
      }

      str.assign( utf8, cLen ) ;
*/

      CHAR *p = JS_EncodeString ( cx , jsStr ) ;
      if ( NULL != p )
      {
         str.assign( p ) ;
         free( p ) ;
      }
   }
done:
//   if ( NULL != utf8 )
//   {
//      SDB_OSS_FREE( utf8 ) ;
//   }
   return rc ;
}

INT32 sptConvertor::_toString( const jsval &val, std::string &str )
{
   return toString( _cx, val, str ) ;
}

INT32 sptConvertor::_toInt( const jsval &val, INT32 &iN )
{
   INT32 rc = SDB_OK ;
   int32 ip = 0 ;
   if ( !JS_ValueToInt32( _cx, val, &ip ) )
   {
      rc = SDB_INVALIDARG ;
      goto error ;
   }
   iN = ip ;
done:
   return rc ;
error:
   goto done ;
}

INT32 sptConvertor::_toDouble( const jsval &val, FLOAT64 &fV )
{
   INT32 rc = SDB_OK ;
   jsdouble dp = 0 ;
   if ( !JS_ValueToNumber( _cx, val, &dp ))
   {
      rc = SDB_INVALIDARG ;
      goto error ;
   }
   fV = dp ;
done:
   return rc ;
error:
   goto done ;
}

INT32 sptConvertor::_toBoolean( const jsval &val, BOOLEAN &bL )
{
   INT32 rc = SDB_OK ;
   JSBool bp = TRUE ;
   if ( !JS_ValueToBoolean( _cx, val, &bp ) )
   {
      rc = SDB_INVALIDARG ;
      goto error ;
   }
   bL = bp ;
done:
   return rc ;
error:
   goto done ;
}

BOOLEAN sptConvertor::_isValidOid( const CHAR *value )
{
   if ( NULL == value || 24 > ossStrlen( value ) )
      return FALSE ;
   for ( UINT32 i = 0; i < 24; ++i )
   {
      if ( ! ( ( value[i] >= '0' && value[i] <= '9' ) ||
               ( value[i] >= 'a' && value[i] <= 'f' ) ||
               ( value[i] >= 'A' && value[i] <= 'F' ) ) )
      {
         return FALSE ;
      }
   }
   return TRUE ;
}
