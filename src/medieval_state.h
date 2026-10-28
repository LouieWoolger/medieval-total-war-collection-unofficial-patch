#ifndef MTW_MEDIEVAL_STATE_H
#define MTW_MEDIEVAL_STATE_H
/* Validated Medieval wire records. Parsed JSON is not mutation authority.
   A typed receipt/journal borrows its document and identity. Keep all alive.
   Original/staged-file verification is separate and mandatory before mutation. */
#include "patch_files.h"
#include "patch_registry.h"
#include "direct_exe_patch.h"
#include "campaign_locale_patch.h"
#include "game_data_patch.h"
#include "../include/credits_payload_hashes.h"
#include "../include/credits_payload_previous_hashes.h"
#include "../include/credits_payload_r288_hashes.h"
#include "../include/credits_payload_r289_hashes.h"
#include "../include/credits_payload_r290_hashes.h"
#include "../include/credits_payload_r291_hashes.h"
#include "../include/credits_payload_r292_hashes.h"
#include "../include/runtime_proxy_hashes.h"

#define MEDIEVAL_STATE_NAME ".unofficial-medieval-total-war-patch"
#define MEDIEVAL_RECEIPT_NAME "install-manifest.json"
#define MEDIEVAL_RECEIPT_SCHEMA "unofficial-medieval-total-war-patch-install-v8"
#define MEDIEVAL_RECEIPT_SCHEMA_V7 "unofficial-medieval-total-war-patch-install-v7"
#define MEDIEVAL_RECEIPT_SCHEMA_V6 "unofficial-medieval-total-war-patch-install-v6"
#define MEDIEVAL_RECEIPT_SCHEMA_V5 "unofficial-medieval-total-war-patch-install-v5"
#define MEDIEVAL_RECEIPT_SCHEMA_V4 "unofficial-medieval-total-war-patch-install-v4"
#define MEDIEVAL_RECEIPT_SCHEMA_V3 "unofficial-medieval-total-war-patch-install-v3"
#define MEDIEVAL_RECEIPT_SCHEMA_V2 "unofficial-medieval-total-war-patch-install-v2"
#define MEDIEVAL_LEGACY_SCHEMA "unofficial-medieval-total-war-patch-install-v1"
#define MEDIEVAL_JOURNAL_SCHEMA "unofficial-medieval-patch-transaction-v2"
#define MEDIEVAL_EXECUTABLE_HASH "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"
#define MEDIEVAL_SPRITE_DISABLED_PROXY_HASH "DAB1FF9ADBA510206BC182D975DBFFA0ADC0E4A9637CA2507D4091F33E48F7C7"
#define MEDIEVAL_R199_SPRITE_DISABLED_PROXY_HASH "ACDCCA09E28FD13715FA6E8D99E429B2422A37B22E5C5FBE7CCDCA838D418AB3"
#define MEDIEVAL_R198_SPRITE_DISABLED_PROXY_HASH "37F8C4CCAB491F863CC91639637876696F51C243E4E26DE0649B8B45099CBD6C"
#define MEDIEVAL_R197_SPRITE_DISABLED_PROXY_HASH "B0394B8A79A178C51F8F8393B4826FB959DAE98413F6476E367C5DB8F4202AF7"
#define MEDIEVAL_R195_SPRITE_DISABLED_PROXY_HASH "B620EFA4334DA6D8317DD7EAEC22805F04C16A01BC947373271B7E89635EC0A9"
#define MEDIEVAL_FAST_ONLY_PROXY_HASH "541168074FCA1CD213E59C8E0F7700E12192E01CFE9B3966CE31D85F36D454E2"
#define MEDIEVAL_R201_TERRAIN_ICON_HASH "9BE59686AB10B1823A2AAA7F01689E77ECB0A824B92C8D60C6D85CC556538EEB"
#define MEDIEVAL_R201_TERRAIN_FAST_ICON_HASH "7D6C94A4CF0542BA5B64046E607F0FF80DD1E1F90FF2CB19A979E31D343610B3"
#define MEDIEVAL_R201_TERRAIN_NO_ICON_HASH "7577E700E9DE772FB6117689B8BACE2B1137C512D291A5532A3785476AC65743"
#define MEDIEVAL_R201_TERRAIN_FAST_NO_ICON_HASH "AD2A580188AE30092D2E570592E525C1DE37BE5CC26BC3DFAE8FB5FE878D0B87"
#define MEDIEVAL_R201_ICON_ONLY_HASH "13CC413BC03477BD053E2A6026C0B51791C0DEADB67D9EBBB53696DF74A623A4"
#define MEDIEVAL_R201_FAST_ICON_HASH "77413C55AFBE182D061A8C83D1F83E352FBC8F6D917DD35973B8BF76E04E9D02"
/* Keep the released R201 identities for v5 receipt upgrades. R202 was
   rebuilt with the 64-state EXE table needed by Character Parchment Fix. */
#define MEDIEVAL_R202_TERRAIN_ICON_HASH "E8E09A4404E9604666916E29B2A31D8F2F718ABDC549F952F3A85930E39E35E2"
#define MEDIEVAL_R202_TERRAIN_FAST_ICON_HASH "18ABF59CC3D739ADCEE0A54FFFDCC054877F44ED23ADDA104B45BD1E186B8BD2"
#define MEDIEVAL_R202_TERRAIN_NO_ICON_HASH "7586A362788583C6D620E7683476ABF4A083B81A5122A9EE37790176306493C1"
#define MEDIEVAL_R202_TERRAIN_FAST_NO_ICON_HASH "31B3A56F7EC715EA797AEA140126B581602D735E65F021C1C6F7CD3741EED933"
#define MEDIEVAL_R202_FAST_ONLY_PROXY_HASH "BF548CA7E2D164CB63867E0B826F6BF191E89216D4FAD0F371D081F926BCF548"
#define MEDIEVAL_R202_FAST_ICON_HASH "CAC0963CA6638ED64C303995AA0E93455BD3C8FD7537BCCB849D9BD2375D9937"
#define MEDIEVAL_R202_ICON_ONLY_HASH MEDIEVAL_R201_ICON_ONLY_HASH
/* The Zoom variants share R202's exact game and graphics dependencies but
   contain the independently selected campaign-tray option. */
#define MEDIEVAL_R203_TERRAIN_ICON_ZOOM_HASH "ACBC600FDF5053F87957C1561260FD9DBCE0B4DB1C03183626172328EB627F8D"
#define MEDIEVAL_R203_TERRAIN_FAST_ICON_ZOOM_HASH "02AD07037ED7D1A1FAAE27C1BD474FCF965E256BBD8E55EFFFE761B17C645646"
#define MEDIEVAL_R203_TERRAIN_NO_ICON_ZOOM_HASH "2AF9A905859D12EA25836CE5496D5C22BDD8832D2EC0EE224CF5F79C2F68EC87"
#define MEDIEVAL_R203_TERRAIN_FAST_NO_ICON_ZOOM_HASH "5D6721FE12BC29DE2DCBF800931CE1A9A8115D4FE96DC3F207EE6B23B1EE2EDE"
#define MEDIEVAL_R203_ZOOM_ONLY_HASH "48AFDD4DADFD1FC7C519BBAFCD62140DE7B2BF9157F9745F3E773AA27017DCB6"
#define MEDIEVAL_R203_FAST_ZOOM_HASH "B2A4CCE0FAAC22FB327CA9862F27727B6D314868345A2DD98078F3EF4D7AA1BA"
#define MEDIEVAL_R203_ICON_ZOOM_HASH "CA201E928610960C34A8A5ECEA153A79E7244045543EB7266DED2C894D9A447E"
#define MEDIEVAL_R203_FAST_ICON_ZOOM_HASH "78BC357CAC6B122FA00D0A805DE96AACBC997BC8F169F53603242BF55CAC234F"
/* Active Credits-off proxies rebuilt against the native-layout-aware key table. */
#define MEDIEVAL_R288_TERRAIN_ICON_HASH "E0FCBA01968F5A2D870B993D77A0DC85F9AFBB033182423524418A4CD94627B0"
#define MEDIEVAL_R288_TERRAIN_FAST_ICON_HASH "6715A60A5BF3B6B49A5A2DD228FEFC68299D2483D5C544BC73C9F9B8470BD2B9"
#define MEDIEVAL_R288_TERRAIN_NO_ICON_HASH "B54928220E7E1899B375C5443E2F2065102C676078E13CCC72DCFA1F0E6C4890"
#define MEDIEVAL_R288_TERRAIN_FAST_NO_ICON_HASH "FD4A9D2FD4C980A0708E8E0C291349EDC4695112D51BF586171F2CAAFB2EA81E"
#define MEDIEVAL_R288_FAST_ONLY_HASH "591D4396037766A9CC04D3B7529994836D49584DBAC3E25FDC33A2851A15FAF0"
#define MEDIEVAL_R288_ICON_ONLY_HASH "586A89BB150F2F8E8C9D1BA299E9B8FC0EB1FF344F42ECC98D4956A8DE313312"
#define MEDIEVAL_R288_FAST_ICON_HASH "892B82BC57B44CD0308BBDF4AA4A21D37B12B62B42470B3DB51C72494D9230A2"
#define MEDIEVAL_R288_TERRAIN_ICON_ZOOM_HASH "94984C87693B9C2A74380F326BE1AA8FE0BA10EECE79E1733914262308E3B5EF"
#define MEDIEVAL_R288_TERRAIN_FAST_ICON_ZOOM_HASH "C421CB76739287396B8B90F6C572ED8E939E97061D8068BD753A2C1CA83176B7"
#define MEDIEVAL_R288_TERRAIN_NO_ICON_ZOOM_HASH "674F703B74BF69F5AE42284AB235456CB42B23DE8EA57FE3A3501FE07ACAD252"
#define MEDIEVAL_R288_TERRAIN_FAST_NO_ICON_ZOOM_HASH "976CDC8AD6EB9DF438BE756BCC1D785D4468771136E5024B1A4B1F778F72286B"
#define MEDIEVAL_R288_ZOOM_ONLY_HASH "FF87DABA5FFF8163977021ECA14EEA81E8ECEBB4BFCFFA9D50366E9BD61D8DDC"
#define MEDIEVAL_R288_FAST_ZOOM_HASH "F8F8512E357D32FBC881B38C0B4FAD61248036B1A23ADCD41C0A1B716510FAC4"
#define MEDIEVAL_R288_ICON_ZOOM_HASH "BC0BCE4064DE27DC4D943BFD8B2E3FA67DEACE10D595710B4F6419F497B30175"
#define MEDIEVAL_R288_FAST_ICON_ZOOM_HASH "3A1F502F5333A8E95EBE52B07E7ACEAE9467F4809E0D84D8628714A9362005C2"
/* The campaign text change requires new runtime identities. Retain R288 above
   so receipts from the previously delivered installer can be upgraded. */
#define MEDIEVAL_R289_TERRAIN_FAST_ICON_HASH "5A249419EF7BEC0DDEADAD94569EF07ADF4D1857C424340AF79C352FAB1CB081"
#define MEDIEVAL_R289_TERRAIN_FAST_NO_ICON_HASH "227002C535067A4ED524275EE96B5C89FC9462229AAAF539AFBA33FBF2A72DF4"
#define MEDIEVAL_R289_FAST_ONLY_HASH "019AF95C61B3B9827CA61E4AA412570D4F0AA008F737044C2D0ED76B972E6B4E"
#define MEDIEVAL_R289_FAST_ICON_HASH "B122FEE1D47196CDF14127E9A621CC9721DE932FEF6E955CBFA53FDAFCB550F1"
#define MEDIEVAL_R289_TERRAIN_ICON_ZOOM_HASH "7CB59AC206CB31198491BD76DB5889E15E373430B5241ECA3C4C4DF51795A769"
#define MEDIEVAL_R289_TERRAIN_FAST_ICON_ZOOM_HASH "F16A1B3DFCF858782683D883FFA4898180A2A90A79ADCAA7794B9F5D63A92CDD"
#define MEDIEVAL_R289_TERRAIN_NO_ICON_ZOOM_HASH "3C151027A61F324B0AA0FB3FCC5125894650709393988B641A09FB3E4BB65329"
#define MEDIEVAL_R289_TERRAIN_FAST_NO_ICON_ZOOM_HASH "DC2C39691AF9E3586CA88F4782D598453C8E2F95D2D57DAF4FD2311A6291C34B"
#define MEDIEVAL_R289_ZOOM_ONLY_HASH "B70612DB391A9D7BCD1A13685FB156C50F297A42131B47368648C8065CE0F4C3"
#define MEDIEVAL_R289_FAST_ZOOM_HASH "199977EA32E6167E9E23003DB90114E353768536AFDF564F896EC7AA7B41DD0E"
#define MEDIEVAL_R289_ICON_ZOOM_HASH "7D3DA7D39656CD7B22E8BD492DB6CC7B1C11EA991D34D5F706411C0E5C807485"
#define MEDIEVAL_R289_FAST_ICON_ZOOM_HASH "31FEE709711239AB08752DE3ED148A29EAA894437A11076A43D2062F4E1A6C8F"
/* Current proxies recognise the updated zoom tooltip locale. R289 identities
   above remain valid for upgrading installations with the previous wording. */
#define MEDIEVAL_R290_TERRAIN_FAST_ICON_HASH "DE5269832BA4CA8BFED6FC2E6F8EEB2013A3926B71084336E39DE4F9D7F1EF7D"
#define MEDIEVAL_R290_TERRAIN_FAST_NO_ICON_HASH "7DC0431A69A6DB9ECABC66E6F558ADD00CD4C93D3FAC822D19C741759587DB0C"
#define MEDIEVAL_R290_FAST_ONLY_HASH "07BF6159F17C5F6E645E60FB180C38752C8BCE8DBC9FB9116BF7F4373297DC98"
#define MEDIEVAL_R290_FAST_ICON_HASH "93E4FE0B05992F024FEB55E64D399D29CA59934A3D3DEE426205D98C1953DD67"
#define MEDIEVAL_R290_TERRAIN_ICON_ZOOM_HASH "B0DF2F26A2EDA82535810CEA56EE9658340BB27E2F8168E6B4EA669EA3C1F536"
#define MEDIEVAL_R290_TERRAIN_FAST_ICON_ZOOM_HASH "5870D7E7E7EF5998CB98B48236DFB73C4C9233E8B1CAAA12FCC5425119CEFD9E"
#define MEDIEVAL_R290_TERRAIN_NO_ICON_ZOOM_HASH "794420F548AD39906D3FB03C279F972B35611B7EE4F625CA6DC2C248C312F426"
#define MEDIEVAL_R290_TERRAIN_FAST_NO_ICON_ZOOM_HASH "5742EAB54C95934F864F61DED2AFA3D161935471CDBAB27634046890FE23E300"
#define MEDIEVAL_R290_ZOOM_ONLY_HASH "9C76E1241A1ABFBCB15AADE8CE74511F170FF9EA2754A9C23DE61B0DBE48A0BF"
#define MEDIEVAL_R290_FAST_ZOOM_HASH "607C91720BDEF377FEA9AEED9CE53E5F7BFDFD63F8FC129A0FBCA3E55F790B1A"
#define MEDIEVAL_R290_ICON_ZOOM_HASH "A898AEAF9D1ADB96CC5F8A011B5EBEDAF11F934305D3270B9F407D28DBB79609"
#define MEDIEVAL_R290_FAST_ICON_ZOOM_HASH "2D37273CB07C8E65BE83EEFD12EEC1A3230269F06A9E09B32F587D9A5AF0EE98"
/* Current terrain variants synchronize the deployment backdrop while retaining
   FastVideoMemoryAccess=true. Unchanged feature-only variants keep their R290
   identities, and the released terrain hashes above remain valid for upgrades. */
#define MEDIEVAL_R291_TERRAIN_ICON_HASH "933DAE4EB657E5A435FDD9B79BD3515FC295F53EA27CDBA9B319C568616D24B4"
#define MEDIEVAL_R291_TERRAIN_FAST_ICON_HASH "C98852131398C39ECA15BD7D1B9C9798FD71DF75B1F2501730C1DC844763663B"
#define MEDIEVAL_R291_TERRAIN_NO_ICON_HASH "FDF6D2E069B2001DBE4BD8A4052E92109072FDB472B25B966A08343F5E73C2E0"
#define MEDIEVAL_R291_TERRAIN_FAST_NO_ICON_HASH "F73026D44289DF0A7AE6321AECE6A38ADAA5B916FE0AEC32A896CE0A89735959"
#define MEDIEVAL_R291_TERRAIN_ICON_ZOOM_HASH "A36AB07DDD773E46428A0C2107CC5B59616B0B08AE0DBB20C2093C235959B13B"
#define MEDIEVAL_R291_TERRAIN_FAST_ICON_ZOOM_HASH "5CDFDA02727420205B2795F988858331085AA178DA31CDB32FB14DBC3D589717"
#define MEDIEVAL_R291_TERRAIN_NO_ICON_ZOOM_HASH "ECD47B8A3B380E48EE1E11191D8B3F267AB485189137406F13E432FB5EC54B8E"
#define MEDIEVAL_R291_TERRAIN_FAST_NO_ICON_ZOOM_HASH "EC25CB80CB3FE339D39D438E2295D3D9F9FD3B2AB13004BAB56E488535ED5421"
/* Current terrain variants keep the deployment fix and force the verified
   texture-alpha dust shader for each selected dust draw. R291 stays pinned
   above for upgrades from the previous local build. */
#define MEDIEVAL_R292_TERRAIN_ICON_HASH "E7BBEB5B34756B359F8CD61BA3EDB88F6DFFC0DED52091F4BB5534B84ADABAA4"
#define MEDIEVAL_R292_TERRAIN_FAST_ICON_HASH "64623E7C9347F8C458108AC23C976999E1AF9E3001D77B64E9F01167C6D1CE9B"
#define MEDIEVAL_R292_TERRAIN_NO_ICON_HASH "8738079E7418AD43A2195EA1BFE42F55DC29717190B542EC05E4182A3C33CBE2"
#define MEDIEVAL_R292_TERRAIN_FAST_NO_ICON_HASH "D4ACE2D3C842A29489BA866CC34B7DA9FD3F14823E8755CCF4F9A9CD4E0EBFA3"
#define MEDIEVAL_R292_TERRAIN_ICON_ZOOM_HASH "410842B9EE5457A48348959FB7E28A2C2DA745DC3A969612AFD9037F4DA3C988"
#define MEDIEVAL_R292_TERRAIN_FAST_ICON_ZOOM_HASH "4A413BD765F83DD6B6EC1D3EBE682861061AF262250A3FD8FC4ACD6E89B9BDF5"
#define MEDIEVAL_R292_TERRAIN_NO_ICON_ZOOM_HASH "2FEFFA8ED1D7ED918BC78FA81F72749139B838D215BCAE9FAF0E26EF916F15E2"
#define MEDIEVAL_R292_TERRAIN_FAST_NO_ICON_ZOOM_HASH "914E9DEFAB45C5EBF957D8AE67497BCDE72EB0555CD44173669D175CA4F9ADE5"
/* R204 rebuilds the current proxies with the optional key-section identities.
   Keep R202/R203 above for receipts written by the previously delivered build. */
#define MEDIEVAL_R204_TERRAIN_ICON_HASH "A29AD8BF49FC709CACDDF549D8E72161CE5A2B9B05F4B4A99524EBA7278CD44F"
#define MEDIEVAL_R204_TERRAIN_FAST_ICON_HASH "4B665EBDC1B9D9F59535605B4CDAA1DD5914289DF125300A4F863E3B2B623104"
#define MEDIEVAL_R204_TERRAIN_NO_ICON_HASH "8F01F5EE49517AD03018926D027E71DB2C1940E93D5698714DFC5A8CD0343E5F"
#define MEDIEVAL_R204_TERRAIN_FAST_NO_ICON_HASH "92DF7108E1B4C4BF3D9BBF0940CD59399E2D8B66E6321A7BE00A0D45AE1572D1"
#define MEDIEVAL_R204_FAST_ONLY_HASH "FF0D91C564883986FEEF5047BD473C27669A23090832AE5BC115F852CCDD2EDC"
#define MEDIEVAL_R204_FAST_ICON_HASH "E59699B73D02B7605FB0DBFCB7F96232C3C1FF7C81527E0320DCCF3D86EECBC4"
#define MEDIEVAL_R204_ICON_ONLY_HASH MEDIEVAL_R202_ICON_ONLY_HASH
#define MEDIEVAL_R204_TERRAIN_ICON_ZOOM_HASH "ED04B72F84F6FC8028D4B8EAA37EAB40E87F7BE110C63B263101EE84E970BD00"
#define MEDIEVAL_R204_TERRAIN_FAST_ICON_ZOOM_HASH "B9F3CA94C66F09720CF8873474FBCB38902D20682D85132246A153A038F045D9"
#define MEDIEVAL_R204_TERRAIN_NO_ICON_ZOOM_HASH "91406A1E783EC537BF187D0D35D3C584A26EA087046DED88718AAE9F4B4F8826"
#define MEDIEVAL_R204_TERRAIN_FAST_NO_ICON_ZOOM_HASH "8C2023FA23EE1404F26E7D3115C3E5CA4F71BE58D3FE2B9E7959507C34159038"
#define MEDIEVAL_R204_ZOOM_ONLY_HASH "CF3B8F88A94ADF464F5639E152A0343C5EBC29720D02275A4D3D83178DB36EE5"
#define MEDIEVAL_R204_FAST_ZOOM_HASH "60E92B31ADE1E68246AD8A745570294570C2AE4AB198A00B35DA9AF47699724A"
#define MEDIEVAL_R204_ICON_ZOOM_HASH "69AF77FE8B941EB2217980467B3607FFA290DEA83B1DEF47EAF3A01F0E192A1B"
#define MEDIEVAL_R204_FAST_ICON_ZOOM_HASH "473EE55A6D6095D0F242D161614358BD02B1D8B4FBA4DBEA4D21905BEF46574A"
/* R205 rejects occupied key captures in the game's own Assign Keys display.
   R204 remains pinned above so experimental installs can be identified. */
#define MEDIEVAL_R205_TERRAIN_ICON_HASH "5725E8D4910B59864AA5275AC1E3AA112BF336AE3A8A3E107C11DB55EEAE5772"
#define MEDIEVAL_R205_TERRAIN_FAST_ICON_HASH "6E5733EB829047066197572A0A4476611888C48843127645550B726C989A5243"
#define MEDIEVAL_R205_TERRAIN_NO_ICON_HASH "EADA8418D21700D9605E825021DB2E7FA08397BA350E5B5E987B980F2A5FF713"
#define MEDIEVAL_R205_TERRAIN_FAST_NO_ICON_HASH "BD771109CBBA00AB1B1E3521A8BCA2340C6A103819CC48057F63CC41A48A15B1"
#define MEDIEVAL_R205_FAST_ONLY_HASH "C3A13F28C48FE53F3E21DF5C3A07F9571AD87B75B3322D6ECE3FD8C6C7C2A8B8"
#define MEDIEVAL_R205_FAST_ICON_HASH "264F798A2983F21DA8F8EA1A6D55A04A43138706A1082E49F9840394C32006E5"
#define MEDIEVAL_R205_ICON_ONLY_HASH "13CC413BC03477BD053E2A6026C0B51791C0DEADB67D9EBBB53696DF74A623A4"
#define MEDIEVAL_R205_TERRAIN_ICON_ZOOM_HASH "831007C7E94BBF67CED8304F7D0680915F5AC3503FADBB1D30BA258AA45D176A"
#define MEDIEVAL_R205_TERRAIN_FAST_ICON_ZOOM_HASH "3D94D976B33AC1A6E48DF8BE4A8407AB160816D64617656A59F3D8A6CED26450"
#define MEDIEVAL_R205_TERRAIN_NO_ICON_ZOOM_HASH "2C07BC50D95189C87FC20BD6E870AA8F0F72097CCA89804552617A888369F0EC"
#define MEDIEVAL_R205_TERRAIN_FAST_NO_ICON_ZOOM_HASH "02AF2E4D49C9BD81AF1FBC3F488AD40FEF50FFE5589028F838ADC31D561C6E04"
#define MEDIEVAL_R205_ZOOM_ONLY_HASH "7E6710902C691FC29ACBC3DF331ADBF3B49719C48B2E1AA33A37C568B6B9249D"
#define MEDIEVAL_R205_FAST_ZOOM_HASH "7F73DA17ED7584ED4737C06F00BA801B572E83E367E896845BA04F2F0CA138C9"
#define MEDIEVAL_R205_ICON_ZOOM_HASH "585549778FA2BAF1D6EE3AEB3B276BF5DEFED33D1C374A8A446D8B3AD75F6E6E"
#define MEDIEVAL_R205_FAST_ICON_ZOOM_HASH "BED2E7AF9F64435250C09451C534401D815032F851ADB5F0A1E2AF6F5B0C6762"
#define MEDIEVAL_R199_FAST_ONLY_PROXY_HASH "1A70E91A7575D881C57FF41BB318312B41D4BB9F5ED36E7577260716075CAED0"
#define MEDIEVAL_TERRAIN_FAST_PROXY_HASH "0AA13EA53444BE53AF75B2F9F05F7373C757E68FBE0E19640ED8EAB99F744FB5"
#define MEDIEVAL_R199_TERRAIN_FAST_PROXY_HASH "660CDB3A233213F64B2BD0913D6D814AF8B49D7549051EC4DCAA770C365758ED"
#define MEDIEVAL_R198_TERRAIN_FAST_PROXY_HASH "2B0799454727FDA01E107F3236AB70D8BB0E9C0C6814C730F2E5BB5765E7CF33"
#define MEDIEVAL_R197_TERRAIN_FAST_PROXY_HASH "A7CEAA57C7D814A120EC3FDCA288DF232F65EF6950E7EB0D924EC4582A8E168B"
#define MEDIEVAL_R196_FAST_ONLY_PROXY_HASH "5F1D5FCDBEB7EC9C74355423D23A2E82DAEE015CE444AD24F51CDF2FAB5CB0A2"
#define MEDIEVAL_R196_TERRAIN_FAST_PROXY_HASH "3F10467E0FD6E90D3FB33D4886ED29B94F6242A48E8DE4639969BA1709916159"
#define MEDIEVAL_PREVIOUS_FAST_ONLY_PROXY_HASH "BFC72CFE443CF4AABD9B07ADB7B26145CCEACD2EEF7D150B6A0C6B3E65396049"
#define MEDIEVAL_PREVIOUS_TERRAIN_FAST_PROXY_HASH "28888291DC1057235B8D41A4A6A78B4C7BF3A3716F0387F6536482E734A342AA"
#define MEDIEVAL_R194_SPRITE_DISABLED_PROXY_HASH "2E0680889E855FB2E5931DC7D0971B875196E8E05231E926BF0DDA42C8FAF7FE"
#define MEDIEVAL_R191_SPRITE_DISABLED_PROXY_HASH "F8883FDECB59BD0A57B0C1B6D446A3A096F9ACF89097B784A1189F51210CE551"
#define MEDIEVAL_R189_SPRITE_DISABLED_PROXY_HASH "09333B64AB577BD0DBFAFC5ACC26A8E19AFA82556FED3C414D3433B22315A243"
#define MEDIEVAL_R188_SPRITE_DISABLED_PROXY_HASH "850861A4C92211099656ED76B06B8C443CEF162FEDD23AB6DE70DD7181704EDB"
#define MEDIEVAL_R187_SPRITE_DISABLED_PROXY_HASH "064C234EB46ABDC690F42C5AFE0BFBC61F2D388DE63F9F28D23A27E064E0D6A7"
#define MEDIEVAL_R186_SPRITE_DISABLED_PROXY_HASH "300373700D0868CF2B1BA94762132A781E70918A01DB873DA3E8666FCD73B8F1"
#define MEDIEVAL_RUNTIME_COUNT 5u
#define MEDIEVAL_FILE_COUNT 10u
static const char *const medieval_payload_names[] = {"dgVoodoo_D3D9.dll", "ddraw.dll", "D3DImm.dll",
                                                     "dgVoodoo.conf", "D3D9.dll", "Medieval_TW.exe",
                                                     "Loc/Eng/CHANGES.TXT", "campmap/names/DEFAULT_HEROES.TXT",
                                                     "Loc/Eng/NAMES.TXT", "Loc/Eng/DEFAULT_REGION_SPECIFIC.TXT"};

typedef struct {
    PatchContext context;
    PatchGuard *guard; /* borrowed, opened canonical directory */
    PatchRegistry registry;
    char *target, *owner_sid;
    size_t target_length;
    char registration_key[57], mutex_name[64];
} MedievalIdentity;
typedef struct {
    const char *name, *snapshot_relative, *sidecar_relative, *legacy_original_sha256;
    int sidecar_created;
    PatchFileRecord original, installed; /* wire decoding always clears file IDs */
    const JsonValue *json;
} MedievalFileState;
typedef struct {
    int legacy;
    int old_v2;
    int old_v3;
    int old_v4;
    int old_v5;
    int old_v6;
    int old_v7;
    int name_fix_enabled;
    int marriage_title_fix_enabled;
    int credits_fix_enabled;
    int key_fix_enabled;
    const JsonValue *json;
    const char *installation_id, *status, *target_directory, *installer_version, *preinstall_mode,
        *uninstaller_sha256;
    MedievalFileState files[MEDIEVAL_FILE_COUNT];
} MedievalReceipt;

static inline size_t medieval_receipt_file_count(const MedievalReceipt *r) {
    return r->legacy || r->old_v2 ? MEDIEVAL_RUNTIME_COUNT :
           r->old_v3 || r->old_v4 ? 6u : r->old_v5 ? 7u :
           r->old_v6 || r->old_v7 ? 9u : MEDIEVAL_FILE_COUNT;
}
typedef struct {
    int is_registry;
    const JsonValue *json;
    const char *relative, *hive, *view, *name;
    PatchFileRecord before_file, after_file;
    PatchRegistrySnapshot before_registry, after_registry;
} MedievalAction;
typedef struct {
    const JsonValue *json;
    const char *operation, *phase, *installation_id;
    size_t count, started;
    int state_existed;
    MedievalReceipt receipt;
    MedievalAction actions[48];
} MedievalJournal;

static inline int medieval_hash_valid(const char *s) {
    size_t i;
    if (!s || strlen(s) != 64)
        return 0;
    for (i = 0; i < 64; ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'F')))
            return 0;
    return 1;
}
static inline int medieval_guid_valid(const char *s) {
    size_t i;
    int nonzero = 0;
    if (!s || strlen(s) != 36)
        return 0;
    for (i = 0; i < 36; ++i) {
        unsigned c = (unsigned char)s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-')
                return 0;
            continue;
        }
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
            return 0;
        if (c != '0')
            nonzero = 1;
    }
    return nonzero;
}
static inline const char *medieval_string(const JsonValue *object, const char *key, int optional,
                                          PatchError *e) {
    const JsonValue *v = json_get(object, key);
    if (optional && (!v || v->type == JSON_NULL))
        return "";
    return json_cstring(v, e);
}
static inline int medieval_integer(const JsonValue *object, const char *key, int optional, int64_t *out,
                                   PatchError *e) {
    const JsonValue *v = json_get(object, key);
    *out = 0;
    if (optional && (!v || v->type == JSON_NULL))
        return 1;
    if (!v || v->type != JSON_NUMBER)
        return json_invalid(e, "Expected integer state field.");
    *out = v->number;
    return 1;
}
static inline int medieval_boolean(const JsonValue *object, const char *key, int *out, PatchError *e) {
    const JsonValue *v = json_get(object, key);
    *out = 0;
    if (!v || v->type != JSON_BOOL)
        return json_invalid(e, "Expected boolean state field.");
    *out = v->boolean;
    return 1;
}
static inline void medieval_identity_close(MedievalIdentity *identity) {
    if (!identity)
        return;
    patch_context_close(&identity->context);
    identity->guard = NULL;
    identity->target = NULL;
    identity->owner_sid = NULL;
}
static inline int medieval_identity_open(MedievalIdentity *identity, PatchGuard *guard, PatchError *e) {
    if (!identity || identity->guard || !guard || !guard->canonical) {
        patch_error_set(e, "invalid_argument", "An opened canonical guard and empty identity are required.",
                        ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!patch_registry_init(&identity->registry, e) ||
        !patch_wide_to_utf8(&identity->context, guard->canonical, wcslen(guard->canonical), &identity->target,
                            &identity->target_length, e) ||
        !patch_owner_sid(&identity->context, &identity->owner_sid, e) ||
        !patch_registry_identity(&identity->context, guard->canonical, identity->registration_key,
                                 identity->mutex_name, e)) {
        medieval_identity_close(identity);
        return 0;
    }
    identity->guard = guard;
    identity->registry.allocator = identity->context.allocator;
    identity->registry.memory_limit = identity->context.memory_limit;
    return 1;
}
static inline int medieval_integrity_valid(JsonDocument *owner, const JsonValue *value, PatchError *e) {
    char digest[65];
    const char *stored = medieval_string(value, "integrity_sha256", 0, e);
    if (!stored || !medieval_hash_valid(stored))
        return json_invalid(e, "Invalid metadata integrity value.");
    if (!json_integrity(owner, value, digest, e))
        return 0;
    return !strcmp(digest, stored) ? 1 : json_invalid(e, "Metadata integrity check failed.");
}
static inline int medieval_file_record(const JsonValue *value, PatchFileRecord *out, PatchError *e) {
    const char *hash;
    int exists;
    int64_t length;
    PatchFileRecord record = {0};
    memset(out, 0, sizeof(*out));
    if (!medieval_boolean(value, "exists", &exists, e) || !medieval_integer(value, "length", 0, &length, e) ||
        (hash = medieval_string(value, "sha256", 1, e)) == NULL)
        return 0;
    if (length < 0 || (exists && !medieval_hash_valid(hash)) || (!exists && (*hash || length)))
        return json_invalid(e, "Invalid recovery file identity.");
    record.exists = exists;
    record.length = (uint64_t)length;
    if (exists)
        memcpy(record.sha256, hash, 65);
    *out = record;
    return 1;
}
static inline JsonValue *medieval_record_json(JsonDocument *doc, const PatchFileRecord *record,
                                              PatchError *e) {
    JsonValue *out = json_new(doc, JSON_OBJECT, e);
    if (record->length > INT64_MAX || (record->exists && !medieval_hash_valid(record->sha256)) ||
        (!record->exists && record->length)) {
        json_invalid(e, "Invalid file record for serialization.");
        return NULL;
    }
    if (!out || !json_set(doc, out, "exists", json_bool(doc, record->exists, e), e) ||
        !json_set(doc, out, "sha256",
                  record->exists ? json_text(doc, record->sha256, e) : json_new(doc, JSON_NULL, e), e) ||
        !json_set(doc, out, "length", json_number(doc, (int64_t)record->length, e), e))
        return NULL;
    return out;
}
static inline const char *medieval_r204_proxy_hash(int terrain, int fast, int icon, int zoom) {
    if (zoom) {
        if (terrain)
            return fast ? (icon ? MEDIEVAL_R204_TERRAIN_FAST_ICON_ZOOM_HASH :
                                  MEDIEVAL_R204_TERRAIN_FAST_NO_ICON_ZOOM_HASH) :
                          (icon ? MEDIEVAL_R204_TERRAIN_ICON_ZOOM_HASH :
                                  MEDIEVAL_R204_TERRAIN_NO_ICON_ZOOM_HASH);
        return fast ? (icon ? MEDIEVAL_R204_FAST_ICON_ZOOM_HASH : MEDIEVAL_R204_FAST_ZOOM_HASH) :
                      (icon ? MEDIEVAL_R204_ICON_ZOOM_HASH : MEDIEVAL_R204_ZOOM_ONLY_HASH);
    }
    if (terrain)
        return fast ? (icon ? MEDIEVAL_R204_TERRAIN_FAST_ICON_HASH :
                              MEDIEVAL_R204_TERRAIN_FAST_NO_ICON_HASH) :
                      (icon ? MEDIEVAL_R204_TERRAIN_ICON_HASH : MEDIEVAL_R204_TERRAIN_NO_ICON_HASH);
    return fast ? (icon ? MEDIEVAL_R204_FAST_ICON_HASH : MEDIEVAL_R204_FAST_ONLY_HASH) :
                  MEDIEVAL_R204_ICON_ONLY_HASH;
}
static inline const char *medieval_r205_proxy_hash(int terrain, int fast, int icon, int zoom) {
    if (zoom) {
        if (terrain)
            return fast ? (icon ? MEDIEVAL_R205_TERRAIN_FAST_ICON_ZOOM_HASH :
                                  MEDIEVAL_R205_TERRAIN_FAST_NO_ICON_ZOOM_HASH) :
                          (icon ? MEDIEVAL_R205_TERRAIN_ICON_ZOOM_HASH :
                                  MEDIEVAL_R205_TERRAIN_NO_ICON_ZOOM_HASH);
        return fast ? (icon ? MEDIEVAL_R205_FAST_ICON_ZOOM_HASH : MEDIEVAL_R205_FAST_ZOOM_HASH) :
                      (icon ? MEDIEVAL_R205_ICON_ZOOM_HASH : MEDIEVAL_R205_ZOOM_ONLY_HASH);
    }
    if (terrain)
        return fast ? (icon ? MEDIEVAL_R205_TERRAIN_FAST_ICON_HASH :
                              MEDIEVAL_R205_TERRAIN_FAST_NO_ICON_HASH) :
                      (icon ? MEDIEVAL_R205_TERRAIN_ICON_HASH : MEDIEVAL_R205_TERRAIN_NO_ICON_HASH);
    return fast ? (icon ? MEDIEVAL_R205_FAST_ICON_HASH : MEDIEVAL_R205_FAST_ONLY_HASH) :
                  MEDIEVAL_R205_ICON_ONLY_HASH;
}
static inline const char *medieval_r288_proxy_hash(int terrain, int fast, int icon, int zoom) {
    if (zoom) {
        if (terrain)
            return fast ? (icon ? MEDIEVAL_R288_TERRAIN_FAST_ICON_ZOOM_HASH :
                                  MEDIEVAL_R288_TERRAIN_FAST_NO_ICON_ZOOM_HASH) :
                          (icon ? MEDIEVAL_R288_TERRAIN_ICON_ZOOM_HASH :
                                  MEDIEVAL_R288_TERRAIN_NO_ICON_ZOOM_HASH);
        return fast ? (icon ? MEDIEVAL_R288_FAST_ICON_ZOOM_HASH : MEDIEVAL_R288_FAST_ZOOM_HASH) :
                      (icon ? MEDIEVAL_R288_ICON_ZOOM_HASH : MEDIEVAL_R288_ZOOM_ONLY_HASH);
    }
    if (terrain)
        return fast ? (icon ? MEDIEVAL_R288_TERRAIN_FAST_ICON_HASH :
                              MEDIEVAL_R288_TERRAIN_FAST_NO_ICON_HASH) :
                      (icon ? MEDIEVAL_R288_TERRAIN_ICON_HASH : MEDIEVAL_R288_TERRAIN_NO_ICON_HASH);
    return fast ? (icon ? MEDIEVAL_R288_FAST_ICON_HASH : MEDIEVAL_R288_FAST_ONLY_HASH) :
                  MEDIEVAL_R288_ICON_ONLY_HASH;
}
static inline const char *medieval_r289_proxy_hash(int terrain, int fast, int icon, int zoom) {
    if (zoom) {
        if (terrain)
            return fast ? (icon ? MEDIEVAL_R289_TERRAIN_FAST_ICON_ZOOM_HASH :
                                  MEDIEVAL_R289_TERRAIN_FAST_NO_ICON_ZOOM_HASH) :
                          (icon ? MEDIEVAL_R289_TERRAIN_ICON_ZOOM_HASH :
                                  MEDIEVAL_R289_TERRAIN_NO_ICON_ZOOM_HASH);
        return fast ? (icon ? MEDIEVAL_R289_FAST_ICON_ZOOM_HASH : MEDIEVAL_R289_FAST_ZOOM_HASH) :
                      (icon ? MEDIEVAL_R289_ICON_ZOOM_HASH : MEDIEVAL_R289_ZOOM_ONLY_HASH);
    }
    if (fast) {
        if (terrain)
            return icon ? MEDIEVAL_R289_TERRAIN_FAST_ICON_HASH : MEDIEVAL_R289_TERRAIN_FAST_NO_ICON_HASH;
        return icon ? MEDIEVAL_R289_FAST_ICON_HASH : MEDIEVAL_R289_FAST_ONLY_HASH;
    }
    return medieval_r288_proxy_hash(terrain, fast, icon, zoom);
}
static inline const char *medieval_r290_proxy_hash(int terrain, int fast, int icon, int zoom) {
    if (zoom) {
        if (terrain)
            return fast ? (icon ? MEDIEVAL_R290_TERRAIN_FAST_ICON_ZOOM_HASH :
                                  MEDIEVAL_R290_TERRAIN_FAST_NO_ICON_ZOOM_HASH) :
                          (icon ? MEDIEVAL_R290_TERRAIN_ICON_ZOOM_HASH :
                                  MEDIEVAL_R290_TERRAIN_NO_ICON_ZOOM_HASH);
        return fast ? (icon ? MEDIEVAL_R290_FAST_ICON_ZOOM_HASH : MEDIEVAL_R290_FAST_ZOOM_HASH) :
                      (icon ? MEDIEVAL_R290_ICON_ZOOM_HASH : MEDIEVAL_R290_ZOOM_ONLY_HASH);
    }
    if (fast) {
        if (terrain)
            return icon ? MEDIEVAL_R290_TERRAIN_FAST_ICON_HASH : MEDIEVAL_R290_TERRAIN_FAST_NO_ICON_HASH;
        return icon ? MEDIEVAL_R290_FAST_ICON_HASH : MEDIEVAL_R290_FAST_ONLY_HASH;
    }
    return medieval_r289_proxy_hash(terrain, fast, icon, zoom);
}
static inline const char *medieval_r291_proxy_hash(int terrain, int fast, int icon, int zoom) {
    if (terrain) {
        if (zoom)
            return fast ? (icon ? MEDIEVAL_R291_TERRAIN_FAST_ICON_ZOOM_HASH :
                                  MEDIEVAL_R291_TERRAIN_FAST_NO_ICON_ZOOM_HASH) :
                          (icon ? MEDIEVAL_R291_TERRAIN_ICON_ZOOM_HASH :
                                  MEDIEVAL_R291_TERRAIN_NO_ICON_ZOOM_HASH);
        return fast ? (icon ? MEDIEVAL_R291_TERRAIN_FAST_ICON_HASH :
                              MEDIEVAL_R291_TERRAIN_FAST_NO_ICON_HASH) :
                      (icon ? MEDIEVAL_R291_TERRAIN_ICON_HASH :
                              MEDIEVAL_R291_TERRAIN_NO_ICON_HASH);
    }
    return medieval_r290_proxy_hash(terrain, fast, icon, zoom);
}

static inline const char *medieval_r292_proxy_hash(int terrain, int fast, int icon, int zoom) {
    if (terrain) {
        if (zoom)
            return fast ? (icon ? MEDIEVAL_R292_TERRAIN_FAST_ICON_ZOOM_HASH :
                                  MEDIEVAL_R292_TERRAIN_FAST_NO_ICON_ZOOM_HASH) :
                          (icon ? MEDIEVAL_R292_TERRAIN_ICON_ZOOM_HASH :
                                  MEDIEVAL_R292_TERRAIN_NO_ICON_ZOOM_HASH);
        return fast ? (icon ? MEDIEVAL_R292_TERRAIN_FAST_ICON_HASH :
                              MEDIEVAL_R292_TERRAIN_FAST_NO_ICON_HASH) :
                      (icon ? MEDIEVAL_R292_TERRAIN_ICON_HASH :
                              MEDIEVAL_R292_TERRAIN_NO_ICON_HASH);
    }
    return medieval_r290_proxy_hash(terrain, fast, icon, zoom);
}

static inline int medieval_receipt_validate(JsonDocument *owner, const JsonValue *value,
                                            const MedievalIdentity *identity, int allow_legacy,
                                            MedievalReceipt *out, PatchError *e) {
    MedievalReceipt r = {0};
    const char *schema, *target, *id, *sid, *key, *hash, *original, *snapshot, *sidecar, *legacy_hash;
    const JsonValue *files, *rec;
    size_t i;
    int existed, created;
    int64_t length;
    char expected[96];
    memset(out, 0, sizeof(*out));
    /* Historical receipts predate these choices and retain their own file
       counts and journal masks. */
    r.name_fix_enabled = 1;
    schema = medieval_string(value, "schema", 0, e);
    if (!schema)
        return 0;
    r.legacy = !strcmp(schema, MEDIEVAL_LEGACY_SCHEMA);
    r.old_v2 = !strcmp(schema, MEDIEVAL_RECEIPT_SCHEMA_V2);
    r.old_v3 = !strcmp(schema, MEDIEVAL_RECEIPT_SCHEMA_V3);
    r.old_v4 = !strcmp(schema, MEDIEVAL_RECEIPT_SCHEMA_V4);
    r.old_v5 = !strcmp(schema, MEDIEVAL_RECEIPT_SCHEMA_V5);
    r.old_v6 = !strcmp(schema, MEDIEVAL_RECEIPT_SCHEMA_V6);
    r.old_v7 = !strcmp(schema, MEDIEVAL_RECEIPT_SCHEMA_V7);
    if (strcmp(schema, MEDIEVAL_RECEIPT_SCHEMA) &&
        !(allow_legacy && (r.legacy || r.old_v2 || r.old_v3 || r.old_v4 || r.old_v5 || r.old_v6 || r.old_v7)))
        return json_invalid(e, "Unsupported installation receipt schema.");
    if (!r.legacy && !medieval_integrity_valid(owner, value, e))
        return 0;
    target = medieval_string(value, "target_directory", 0, e);
    if (!target)
        return 0;
    if (!r.legacy) {
        id = medieval_string(value, "directory_identity", 0, e);
        sid = medieval_string(value, "owner_sid", 0, e);
        key = medieval_string(value, "registration_key", 0, e);
        hash = medieval_string(value, "uninstaller_sha256", 0, e);
        if (!id || !sid || !key || !hash)
            return 0;
        if (strcmp(target, identity->target) || strcmp(id, identity->guard->identity)) {
            patch_error_set(e, "installation_moved",
                            "Patch state belongs to a different directory or filesystem object.",
                            ERROR_INVALID_DATA);
            return 0;
        }
        if (strcmp(sid, identity->owner_sid)) {
            patch_error_set(e, "wrong_account", "Run the patch with the Windows account that installed it.",
                            ERROR_ACCESS_DENIED);
            return 0;
        }
        if (strcmp(key, identity->registration_key) || !medieval_hash_valid(hash))
            return json_invalid(e, "Invalid removal identity.");
        r.uninstaller_sha256 = hash;
    } else
        r.uninstaller_sha256 = "";
    r.installation_id = medieval_string(value, "installation_id", 0, e);
    r.status = medieval_string(value, "status", 0, e);
    hash = medieval_string(value, "target_executable_sha256", 0, e);
    files = json_get(value, "files");
    if (!r.installation_id || !r.status || !hash)
        return 0;
    if (!medieval_guid_valid(r.installation_id) || strcmp(hash, MEDIEVAL_EXECUTABLE_HASH) ||
        (strcmp(r.status, "installed") && strcmp(r.status, "installing") && strcmp(r.status, "restored")) ||
        !files || files->type != JSON_OBJECT ||
        files->count != medieval_receipt_file_count(&r))
        return json_invalid(e, "Invalid installation identity or file table.");
    r.installer_version = medieval_string(value, "installer_version", 1, e);
    r.preinstall_mode = medieval_string(value, "preinstall_mode", 1, e);
    if (!r.installer_version || !r.preinstall_mode)
        return 0;
    for (i = 0; i < medieval_receipt_file_count(&r); ++i) {
        MedievalFileState *f = &r.files[i];
        f->name = medieval_payload_names[i];
        rec = json_get(files, f->name);
        if (!medieval_boolean(rec, "existed", &existed, e) ||
            !medieval_boolean(rec, "sidecar_created", &created, e))
            return 0;
        hash = medieval_string(rec, "installed_sha256", r.legacy || r.old_v2 ? 0 : 1, e);
        if (!hash || !medieval_integer(rec, "installed_length", 0, &length, e) ||
            ((r.legacy || r.old_v2 || *hash) && (!medieval_hash_valid(hash) || length <= 0)) ||
            (!*hash && length != 0))
            return json_invalid(e, "Invalid installed file record.");
        f->installed.exists = !!*hash;
        if (*hash) memcpy(f->installed.sha256, hash, 65);
        f->installed.length = (uint64_t)length;
        original = medieval_string(rec, "original_sha256", 1, e);
        snapshot = medieval_string(rec, "snapshot_relative", 1, e);
        sidecar = medieval_string(rec, "sidecar_relative", 1, e);
        legacy_hash = medieval_string(rec, "legacy_original_sha256", 1, e);
        if (!original || !snapshot || !sidecar || !legacy_hash ||
            !medieval_integer(rec, "original_length", 1, &length, e))
            return 0;
        if (i == 6u) strcpy(expected, "originals/CHANGES.TXT");
        else if (i == 7u) strcpy(expected, "originals/DEFAULT_HEROES.TXT");
        else if (i == 8u) strcpy(expected, "originals/NAMES.TXT");
        else if (i == 9u) strcpy(expected, "originals/DEFAULT_REGION_SPECIFIC.TXT");
        else snprintf(expected, sizeof(expected), "originals/%s", f->name);
        if ((existed && (strcmp(snapshot, expected) || !medieval_hash_valid(original) || length < 0)) ||
            (!existed && (*snapshot || *original || length)))
            return json_invalid(e, "Unsafe or invalid original record.");
        if (i == 6u) strcpy(expected, "CHANGES.TXT.unofficial-patch.bak");
        else if (i == 7u) strcpy(expected, "DEFAULT_HEROES.TXT.unofficial-patch.bak");
        else if (i == 8u) strcpy(expected, "NAMES.TXT.unofficial-patch.bak");
        else if (i == 9u) strcpy(expected, "DEFAULT_REGION_SPECIFIC.TXT.unofficial-patch.bak");
        else snprintf(expected, sizeof(expected), "%s.unofficial-patch.bak", f->name);
        if ((*sidecar && strcmp(sidecar, expected)) || (created && (!existed || !*sidecar)) ||
            (*legacy_hash && !medieval_hash_valid(legacy_hash)))
            return json_invalid(e, "Unsafe or inconsistent sidecar record.");
        if (i >= 7u && (created || *sidecar || *legacy_hash))
            return json_invalid(e, "Game data originals do not use legacy sidecars.");
        f->original.exists = existed;
        f->original.length = (uint64_t)length;
        if (existed)
            memcpy(f->original.sha256, original, 65);
        f->snapshot_relative = snapshot;
        f->sidecar_relative = sidecar;
        f->sidecar_created = created;
        f->legacy_original_sha256 = legacy_hash;
        f->json = rec;
    }
    if (!r.legacy && !r.old_v2) {
        int terrain, scroll, sprite = 0, tutorial = 0, parchment = 0, economy = 0, key = 0, name_fix = 1, marriage_title_fix = 0, fast = 0, zoom = 0, icon = 0, credits = 0, previous_fast_locale = 0, zoom_locale_valid = 0;
        const JsonValue *tutorial_field = json_get(value, "tutorial_fix_enabled");
        const JsonValue *parchment_field = json_get(value, "parchment_fix_enabled");
        const JsonValue *economy_field = json_get(value, "economy_fix_enabled");
        const JsonValue *key_field = json_get(value, "key_fix_enabled");
        const JsonValue *name_field = json_get(value, "name_fix_enabled");
        const JsonValue *marriage_title_field = json_get(value, "marriage_title_fix_enabled");
        const JsonValue *icon_field = json_get(value, "battle_icon_fix_enabled");
        const JsonValue *zoom_field = json_get(value, "zoom_fix_enabled");
        const JsonValue *credits_field = json_get(value, "credits_flash_fix_enabled");
        MtwExeVariant original_exe, installed_exe;
        const char *delivery = NULL;
        if (!medieval_boolean(value, "terrain_fix_enabled", &terrain, e) ||
            !medieval_boolean(value, "scroll_fix_enabled", &scroll, e))
            return 0;
        if (!r.old_v3) {
            delivery = medieval_string(value, "sprite_delivery", 0, e);
            if (!delivery || strcmp(delivery, "direct-exe") ||
                !medieval_boolean(value, "sprite_fix_enabled", &sprite, e))
                return json_invalid(e, "Invalid direct Sprite delivery metadata.");
        }
        if (!r.old_v3 && !r.old_v4 &&
            !medieval_boolean(value, "fast_moves_fix_enabled", &fast, e)) return 0;
        /* R197-R199 v5 receipts have no tutorial field: the old runtime
           installed this fix implicitly, but their on-disk EXE is unpatched. */
        if (tutorial_field && !medieval_boolean(value, "tutorial_fix_enabled", &tutorial, e)) return 0;
        if (!r.old_v3 && !r.old_v4 && !r.old_v5 && !parchment_field)
            return json_invalid(e, "The character parchment selection is missing.");
        if (parchment_field && !medieval_boolean(value, "parchment_fix_enabled", &parchment, e)) return 0;
        if (economy_field && !medieval_boolean(value, "economy_fix_enabled", &economy, e)) return 0;
        if (key_field && !medieval_boolean(value, "key_fix_enabled", &key, e)) return 0;
        r.key_fix_enabled = key;
        if (key_field && (r.old_v3 || r.old_v4 || r.old_v5 || r.old_v6))
            return json_invalid(e, "An older receipt cannot carry a key fix selection.");
        if (!r.old_v3 && !r.old_v4 && !r.old_v5 && !r.old_v6 &&
            (!name_field || !medieval_boolean(value, "name_fix_enabled", &name_fix, e)))
            return json_invalid(e, "The name correction selection is missing or invalid.");
        if (r.old_v6 && name_field)
            return json_invalid(e, "A v6 receipt cannot carry a name correction selection.");
        r.name_fix_enabled = name_fix;
        if (r.old_v7 || r.old_v6 || r.old_v5 || r.old_v4 || r.old_v3) {
            if (marriage_title_field)
                return json_invalid(e, "An older receipt cannot carry a marriage title selection.");
        } else if (!marriage_title_field ||
                   !medieval_boolean(value, "marriage_title_fix_enabled", &marriage_title_fix, e))
            return json_invalid(e, "The marriage title selection is missing or invalid.");
        r.marriage_title_fix_enabled = marriage_title_fix;
        if (icon_field && !medieval_boolean(value, "battle_icon_fix_enabled", &icon, e)) return 0;
        if (zoom_field && !medieval_boolean(value, "zoom_fix_enabled", &zoom, e)) return 0;
        if (credits_field && !medieval_boolean(value, "credits_flash_fix_enabled", &credits, e)) return 0;
        if (zoom && !icon_field) return json_invalid(e, "A Zoom receipt needs explicit runtime selection metadata.");
        if (credits && (!icon_field || !zoom_field)) return json_invalid(e, "A Credits receipt needs explicit runtime selection metadata.");
        r.credits_fix_enabled = credits;
        if (!r.old_v3 && !r.old_v4 && fast)
            previous_fast_locale = r.files[6].installed.length == MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_SIZE &&
                                   !strcmp(r.files[6].installed.sha256, MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_SHA256);
        if (zoom)
            zoom_locale_valid = mtw_campaign_locale_zoom_identity(r.files[6].installed.sha256,
                                                                  r.files[6].installed.length, fast);
        for (i = 0; i < MEDIEVAL_RUNTIME_COUNT; ++i)
            if (terrain ? !r.files[i].installed.exists :
                ((fast || icon || zoom || credits) && i == 4u ? !r.files[i].installed.exists :
                 !patch_record_equal(&r.files[i].installed, &r.files[i].original)))
                return json_invalid(e, "Inconsistent Terrain runtime selection.");
        if (!icon_field && !r.old_v3 && !r.old_v4 && fast &&
            strcmp(r.files[4].installed.sha256,
                   terrain ? MEDIEVAL_TERRAIN_FAST_PROXY_HASH : MEDIEVAL_FAST_ONLY_PROXY_HASH) &&
            strcmp(r.files[4].installed.sha256,
                   terrain ? MEDIEVAL_R199_TERRAIN_FAST_PROXY_HASH : MEDIEVAL_R199_FAST_ONLY_PROXY_HASH) &&
            (!terrain || strcmp(r.files[4].installed.sha256, MEDIEVAL_R198_TERRAIN_FAST_PROXY_HASH)) &&
            (!terrain || strcmp(r.files[4].installed.sha256, MEDIEVAL_R197_TERRAIN_FAST_PROXY_HASH)) &&
            strcmp(r.files[4].installed.sha256,
                   terrain ? MEDIEVAL_R196_TERRAIN_FAST_PROXY_HASH : MEDIEVAL_R196_FAST_ONLY_PROXY_HASH) &&
            (!previous_fast_locale ||
             strcmp(r.files[4].installed.sha256,
                    terrain ? MEDIEVAL_PREVIOUS_TERRAIN_FAST_PROXY_HASH :
                              MEDIEVAL_PREVIOUS_FAST_ONLY_PROXY_HASH)))
            return json_invalid(e, "Inconsistent Fast computer moves proxy selection.");
        if (!icon_field && !r.old_v3 && !fast && terrain &&
            strcmp(r.files[4].installed.sha256, MEDIEVAL_SPRITE_DISABLED_PROXY_HASH) &&
            strcmp(r.files[4].installed.sha256, MEDIEVAL_R199_SPRITE_DISABLED_PROXY_HASH) &&
            strcmp(r.files[4].installed.sha256, MEDIEVAL_R198_SPRITE_DISABLED_PROXY_HASH) &&
            strcmp(r.files[4].installed.sha256, MEDIEVAL_R197_SPRITE_DISABLED_PROXY_HASH) &&
            strcmp(r.files[4].installed.sha256, MEDIEVAL_R195_SPRITE_DISABLED_PROXY_HASH) &&
            strcmp(r.files[4].installed.sha256, MEDIEVAL_R194_SPRITE_DISABLED_PROXY_HASH) &&
            strcmp(r.files[4].installed.sha256, MEDIEVAL_R191_SPRITE_DISABLED_PROXY_HASH) &&
            strcmp(r.files[4].installed.sha256, MEDIEVAL_R189_SPRITE_DISABLED_PROXY_HASH) &&
            strcmp(r.files[4].installed.sha256, MEDIEVAL_R188_SPRITE_DISABLED_PROXY_HASH) &&
            strcmp(r.files[4].installed.sha256, MEDIEVAL_R187_SPRITE_DISABLED_PROXY_HASH) &&
            strcmp(r.files[4].installed.sha256, MEDIEVAL_R186_SPRITE_DISABLED_PROXY_HASH))
            return json_invalid(e, "A direct Sprite receipt cannot retain an active proxy Sprite hook.");
        if (icon_field && (terrain || fast || icon || zoom || credits)) {
            const MtwCreditsProxyVariant *credits_variant = credits ?
                mtw_credits_proxy_for_options((unsigned int)terrain, (unsigned int)fast,
                                              (unsigned int)icon, (unsigned int)zoom) : NULL;
            const MtwCreditsProxyVariant *previous_credits_variant = credits ?
                mtw_previous_credits_proxy_for_options((unsigned int)terrain, (unsigned int)fast,
                                                       (unsigned int)icon, (unsigned int)zoom) : NULL;
            const MtwCreditsProxyVariant *r288_credits_variant = credits ?
                mtw_r288_credits_proxy_for_options((unsigned int)terrain, (unsigned int)fast,
                                                   (unsigned int)icon, (unsigned int)zoom) : NULL;
            const MtwCreditsProxyVariant *r289_credits_variant = credits ?
                mtw_r289_credits_proxy_for_options((unsigned int)terrain, (unsigned int)fast,
                                                   (unsigned int)icon, (unsigned int)zoom) : NULL;
            const MtwCreditsProxyVariant *r290_credits_variant = credits ?
                mtw_r290_credits_proxy_for_options((unsigned int)terrain, (unsigned int)fast,
                                                   (unsigned int)icon, (unsigned int)zoom) : NULL;
            const MtwCreditsProxyVariant *r292_credits_variant = credits ?
                mtw_r292_credits_proxy_for_options((unsigned int)terrain, (unsigned int)fast,
                                                   (unsigned int)icon, (unsigned int)zoom) : NULL;
            const MtwCreditsProxyVariant *r291_credits_variant = credits ?
                mtw_r291_credits_proxy_for_options((unsigned int)terrain, (unsigned int)fast,
                                                   (unsigned int)icon, (unsigned int)zoom) : NULL;
            const MtwRuntimeProxyVariant *runtime_variant = !credits ?
                mtw_runtime_proxy_for_options((unsigned int)terrain, (unsigned int)fast,
                                              (unsigned int)icon, (unsigned int)zoom) : NULL;
            const char *current_proxy = credits ? (credits_variant ? credits_variant->sha256 : "") :
                (runtime_variant ? runtime_variant->sha256 : "");
            const char *expected_proxy = credits ? (credits_variant ? credits_variant->sha256 : "") : zoom ?
                (terrain ?
                    (fast ? (icon ? MEDIEVAL_R203_TERRAIN_FAST_ICON_ZOOM_HASH : MEDIEVAL_R203_TERRAIN_FAST_NO_ICON_ZOOM_HASH) :
                            (icon ? MEDIEVAL_R203_TERRAIN_ICON_ZOOM_HASH : MEDIEVAL_R203_TERRAIN_NO_ICON_ZOOM_HASH)) :
                    (fast ? (icon ? MEDIEVAL_R203_FAST_ICON_ZOOM_HASH : MEDIEVAL_R203_FAST_ZOOM_HASH) :
                            (icon ? MEDIEVAL_R203_ICON_ZOOM_HASH : MEDIEVAL_R203_ZOOM_ONLY_HASH))) :
                (r.old_v5 ?
                (terrain ?
                    (fast ? (icon ? MEDIEVAL_R201_TERRAIN_FAST_ICON_HASH : MEDIEVAL_R201_TERRAIN_FAST_NO_ICON_HASH) :
                            (icon ? MEDIEVAL_R201_TERRAIN_ICON_HASH : MEDIEVAL_R201_TERRAIN_NO_ICON_HASH)) :
                    (fast ? (icon ? MEDIEVAL_R201_FAST_ICON_HASH : MEDIEVAL_FAST_ONLY_PROXY_HASH) :
                            MEDIEVAL_R201_ICON_ONLY_HASH)) :
                (terrain ?
                    (fast ? (icon ? MEDIEVAL_R202_TERRAIN_FAST_ICON_HASH : MEDIEVAL_R202_TERRAIN_FAST_NO_ICON_HASH) :
                            (icon ? MEDIEVAL_R202_TERRAIN_ICON_HASH : MEDIEVAL_R202_TERRAIN_NO_ICON_HASH)) :
                    (fast ? (icon ? MEDIEVAL_R202_FAST_ICON_HASH : MEDIEVAL_R202_FAST_ONLY_PROXY_HASH) :
                            MEDIEVAL_R202_ICON_ONLY_HASH)));
            const int current_match = !strcmp(r.files[4].installed.sha256, current_proxy) &&
                (credits ? (credits_variant && r.files[4].installed.length == credits_variant->file_length) :
                           (runtime_variant && r.files[4].installed.length == runtime_variant->file_length));
            const int r292_match = !strcmp(r.files[4].installed.sha256,
                credits ? (r292_credits_variant ? r292_credits_variant->sha256 : "") :
                          medieval_r292_proxy_hash(terrain, fast, icon, zoom)) &&
                (!credits || (r292_credits_variant &&
                             r.files[4].installed.length == r292_credits_variant->file_length));
            const int r291_match = !strcmp(r.files[4].installed.sha256,
                credits ? (r291_credits_variant ? r291_credits_variant->sha256 : "") :
                          medieval_r291_proxy_hash(terrain, fast, icon, zoom)) &&
                (!credits || (r291_credits_variant &&
                             r.files[4].installed.length == r291_credits_variant->file_length));
            const int r290_match = !strcmp(r.files[4].installed.sha256,
                credits ? (r290_credits_variant ? r290_credits_variant->sha256 : "") :
                          medieval_r290_proxy_hash(terrain, fast, icon, zoom)) &&
                (!credits || (r290_credits_variant &&
                             r.files[4].installed.length == r290_credits_variant->file_length));
            const int r289_match = !strcmp(r.files[4].installed.sha256,
                credits ? (r289_credits_variant ? r289_credits_variant->sha256 : "") :
                          medieval_r289_proxy_hash(terrain, fast, icon, zoom)) &&
                (!credits || (r289_credits_variant &&
                             r.files[4].installed.length == r289_credits_variant->file_length));
            const int r288_match = !strcmp(r.files[4].installed.sha256,
                credits ? (r288_credits_variant ? r288_credits_variant->sha256 : "") :
                          medieval_r288_proxy_hash(terrain, fast, icon, zoom)) &&
                (!credits || (r288_credits_variant &&
                             r.files[4].installed.length == r288_credits_variant->file_length));
            const int previous_match = !key && !strcmp(r.files[4].installed.sha256,
                credits ? (previous_credits_variant ? previous_credits_variant->sha256 : "") : expected_proxy) &&
                (!credits || (previous_credits_variant &&
                             r.files[4].installed.length == previous_credits_variant->file_length));
            if (!current_match && !r292_match && !r291_match && !r290_match && !r289_match &&
                !r288_match && !previous_match)
                return json_invalid(e, "Inconsistent graphics proxy selection.");
        }
        if (!r.files[5].original.exists || !r.files[5].installed.exists ||
            !mtw_exe_variant_from_hash(r.files[5].original.sha256,
                                       (size_t)r.files[5].original.length, &original_exe) ||
            !mtw_exe_variant_from_hash(r.files[5].installed.sha256,
                                       (size_t)r.files[5].installed.length, &installed_exe) ||
            original_exe.scroll || original_exe.sprite || original_exe.tutorial || original_exe.ui || original_exe.economy || original_exe.key ||
            installed_exe.scroll != (unsigned int)scroll ||
            installed_exe.sprite != (unsigned int)sprite ||
            installed_exe.tutorial != (unsigned int)tutorial ||
            installed_exe.ui != (unsigned int)parchment ||
            installed_exe.economy != (unsigned int)economy ||
            installed_exe.key != (unsigned int)key ||
            installed_exe.laa != original_exe.laa ||
            (!scroll && !sprite && !tutorial && !parchment && !economy && !key &&
             !patch_record_equal(&r.files[5].installed, &r.files[5].original)))
            return json_invalid(e, "Inconsistent direct EXE fix selection.");
        if (!r.old_v3 && !r.old_v4 &&
            (!r.files[6].original.exists ||
             ((fast || zoom) ? strcmp(r.files[6].original.sha256, MTW_CAMPAIGN_LOCALE_STOCK_SHA256) ||
                     r.files[6].original.length != MTW_CAMPAIGN_LOCALE_STOCK_SIZE ||
                     !r.files[6].installed.exists ||
                     (zoom ? !zoom_locale_valid :
                            strcmp(r.files[6].installed.sha256,
                                   previous_fast_locale ? MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_SHA256 : MTW_CAMPAIGN_LOCALE_FAST_SHA256) ||
                            r.files[6].installed.length !=
                                   (previous_fast_locale ? MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_SIZE : MTW_CAMPAIGN_LOCALE_FAST_SIZE)) :
                     !patch_record_equal(&r.files[6].installed, &r.files[6].original))))
            return json_invalid(e, "Inconsistent Fast computer moves localization selection.");
        if (!r.old_v3 && !r.old_v4 && !r.old_v5) {
            const MedievalFileState *heroes = &r.files[7], *names = &r.files[8];
            int heroes_input = heroes->original.exists &&
                ((heroes->original.length == MTW_HEROES_STOCK_SIZE &&
                  !strcmp(heroes->original.sha256, MTW_HEROES_STOCK_SHA256)) ||
                 (heroes->original.length == MTW_HEROES_FIXED_SIZE &&
                  !strcmp(heroes->original.sha256, MTW_HEROES_FIXED_SHA256)));
            int names_stock = names->original.length == MTW_NAMES_STOCK_SIZE &&
                (!strcmp(names->original.sha256, MTW_NAMES_STOCK_SHA256) ||
                 !strcmp(names->original.sha256, MTW_NAMES_CYD_SHA256));
            int names_fixed = names->original.length == MTW_NAMES_FIXED_SIZE &&
                (!strcmp(names->original.sha256, MTW_NAMES_KILIJ_SHA256) ||
                 !strcmp(names->original.sha256, MTW_NAMES_FIXED_SHA256));
            int names_input = names->original.exists && (names_stock || names_fixed);
            if (!heroes->original.exists || !names->original.exists ||
                (name_fix ? (!heroes_input || !names_input || !heroes->installed.exists ||
                 heroes->installed.length != MTW_HEROES_FIXED_SIZE ||
                 strcmp(heroes->installed.sha256, MTW_HEROES_FIXED_SHA256) ||
                 !names->installed.exists || names->installed.length != MTW_NAMES_FIXED_SIZE ||
                 strcmp(names->installed.sha256, MTW_NAMES_FIXED_SHA256)) :
                 (!patch_record_equal(&heroes->installed, &heroes->original) ||
                  !patch_record_equal(&names->installed, &names->original))))
                return json_invalid(e, "Inconsistent character data correction or original baseline.");
        }
        if (!r.old_v7 && !r.old_v6 && !r.old_v5 && !r.old_v4 && !r.old_v3) {
            const MedievalFileState *titles = &r.files[9];
            if (!titles->original.exists ||
                (marriage_title_fix ?
                 (!mtw_game_data_supported_record(&titles->original, MTW_GAME_DATA_REGION_TITLES) ||
                  !titles->installed.exists ||
                  titles->installed.length != MTW_REGION_TITLES_SIZE ||
                  strcmp(titles->installed.sha256, MTW_REGION_TITLES_FIXED_SHA256)) :
                 !patch_record_equal(&titles->installed, &titles->original)))
                return json_invalid(e, "Inconsistent marriage title correction or original baseline.");
        }
    }
    r.json = value;
    r.target_directory = target;
    *out = r;
    return 1;
}
static inline int medieval_receipt_verify_originals(const MedievalIdentity *identity,
                                                    const MedievalReceipt *receipt, PatchError *e) {
    PatchContext local = {0};
    wchar_t *state = NULL, *path = NULL;
    PatchFileRecord actual;
    size_t i;
    int ok = 0;
    local.allocator = identity->context.allocator;
    local.memory_limit = identity->context.memory_limit;
    if (!patch_path_join(&local, identity->guard->canonical, MEDIEVAL_STATE_NAME, &state, e))
        goto done;
    for (i = 0; i < medieval_receipt_file_count(receipt); ++i)
        if (receipt->files[i].original.exists) {
            if (!patch_path_join(&local, state, receipt->files[i].snapshot_relative, &path, e) ||
                !patch_file_record(identity->guard, path, &actual, e))
                goto done;
            if (!patch_record_equal(&actual, &receipt->files[i].original)) {
                json_invalid(e, "A private original is missing or damaged. Preserve the recovery state.");
                goto done;
            }
            patch_context_free(&local, path);
            path = NULL;
        }
    ok = 1;
done:
    patch_context_close(&local);
    return ok;
}
static inline int medieval_text_path_equal(const MedievalIdentity *identity, const char *a, const char *b,
                                           int *equal, PatchError *e) {
    PatchContext local = {0};
    wchar_t *x = NULL, *y = NULL;
    int ok = 0;
    local.allocator = identity->context.allocator;
    local.memory_limit = identity->context.memory_limit;
    if (patch_utf8_to_wide(&local, a, strlen(a), &x, e) && patch_utf8_to_wide(&local, b, strlen(b), &y, e))
        ok = patch_path_equal(&identity->registry.platform, x, y, equal, e);
    patch_context_close(&local);
    return ok;
}
static inline int medieval_registry_string(const PatchRegistry *registry,
                                           const PatchRegistrySnapshot *snapshot, const char *name,
                                           const char **out, PatchError *e) {
    const JsonValue *entry, *v;
    *out = "";
    if (!patch_registry_find(registry, snapshot, name, &entry, e))
        return 0;
    if (!entry)
        return 1;
    v = json_get(entry, "value");
    if (v->type != JSON_STRING)
        return 1;
    *out = json_cstring(v, e);
    return *out != NULL;
}
static inline int medieval_uninstall_command(PatchContext *context, const MedievalIdentity *identity,
                                             int legacy, char **out, PatchError *e) {
    const char *suffix =
        legacy ? "\\" MEDIEVAL_STATE_NAME "\\Uninstall.exe\"" : "\\" MEDIEVAL_UNINSTALL_NAME "\"";
    size_t suffix_length = strlen(suffix), size;
    void *memory = NULL;
    if (!patch_size_add(identity->target_length, suffix_length + 2, &size) ||
        !patch_alloc(context, size, 1, &memory, e))
        return 0;
    *out = (char *)memory;
    (*out)[0] = '"';
    memcpy(*out + 1, identity->target, identity->target_length);
    memcpy(*out + 1 + identity->target_length, suffix, suffix_length + 1);
    return 1;
}
/* An installed after-state must carry the complete identity binding, even when
   a pre-existing empty or personal-only key would be safe to adopt. */
static inline int medieval_registry_installed_identity(const MedievalIdentity *identity,
                                                        const PatchRegistrySnapshot *snapshot,
                                                        const char *installation_id, PatchError *e) {
    const char *product, *owner, *id, *location, *command;
    char *expected = NULL;
    PatchContext local = {0};
    int equal, ok = 0;
    local.allocator = identity->context.allocator;
    local.memory_limit = identity->context.memory_limit;
    if (!snapshot->exists)
        goto conflict;
    if (!medieval_registry_string(&identity->registry, snapshot, "ProductId", &product, e) ||
        !medieval_registry_string(&identity->registry, snapshot, "OwnerSid", &owner, e) ||
        !medieval_registry_string(&identity->registry, snapshot, "InstallationId", &id, e) ||
        !medieval_registry_string(&identity->registry, snapshot, "InstallLocation", &location, e) ||
        !medieval_registry_string(&identity->registry, snapshot, "UninstallString", &command, e))
        goto done;
    if (strcmp(product, MEDIEVAL_PRODUCT_ID) || strcmp(owner, identity->owner_sid) ||
        strcmp(id, installation_id))
        goto conflict;
    if (!medieval_text_path_equal(identity, location, identity->target, &equal, e))
        goto done;
    if (!equal)
        goto conflict;
    if (!medieval_uninstall_command(&local, identity, 0, &expected, e) ||
        !medieval_text_path_equal(identity, command, expected, &equal, e))
        goto done;
    if (!equal)
        goto conflict;
    ok = 1;
    goto done;
conflict:
    patch_error_set(e, "registry_identity_conflict",
                    "Windows uninstall entry belongs to another installation or account.",
                    ERROR_INVALID_DATA);
done:
    patch_context_close(&local);
    return ok;
}
static inline int medieval_registry_ownership(const MedievalIdentity *identity,
                                              const PatchRegistrySnapshot *snapshot,
                                              const char *installation_id, PatchError *e) {
    const JsonValue *v;
    int owned;
    if (!snapshot->exists)
        return 1;
    for (v = snapshot->values->child; v; v = v->next) {
        if (!patch_registry_owned_name(&identity->registry, json_get(v, "name")->string, &owned, e))
            return 0;
        if (owned)
            return medieval_registry_installed_identity(identity, snapshot, installation_id, e);
    }
    return 1;
}
static inline int medieval_legacy_owned(const MedievalIdentity *identity,
                                        const PatchRegistrySnapshot *snapshot, int *owned,
                                        PatchError *e) {
    const char *location, *command, *publisher, *display;
    char *expected = NULL, *trimmed = NULL;
    size_t n;
    PatchContext local = {0};
    void *memory = NULL;
    int equal, ok = 0;
    local.allocator = identity->context.allocator;
    local.memory_limit = identity->context.memory_limit;
    *owned = 0;
    if (!snapshot->exists)
        return 1;
    if (!medieval_registry_string(&identity->registry, snapshot, "InstallLocation", &location, e) ||
        !medieval_registry_string(&identity->registry, snapshot, "UninstallString", &command, e) ||
        !medieval_registry_string(&identity->registry, snapshot, "Publisher", &publisher, e) ||
        !medieval_registry_string(&identity->registry, snapshot, "DisplayName", &display, e))
        goto done;
    if (strcmp(publisher, "Louie Woolger") ||
        (strcmp(display, MEDIEVAL_PRODUCT_NAME) && strcmp(display, "Unofficial Medieval: Total War Patch"))) {
        ok = 1;
        goto done;
    }
    n = strlen(location);
    /* Discovery and recovery share this bounded trailing-separator policy.
       Keep the captured value intact; no child, sibling or prefix normalization. */
    while (n && location[n - 1] == '\\')
        --n;
    if (!patch_alloc(&local, n + 1, 1, &memory, e))
        goto done;
    trimmed = (char *)memory;
    memcpy(trimmed, location, n);
    if (!medieval_text_path_equal(identity, trimmed, identity->target, &equal, e))
        goto done;
    if (!equal) {
        ok = 1;
        goto done;
    }
    if (!medieval_uninstall_command(&local, identity, 1, &expected, e) ||
        !medieval_text_path_equal(identity, command, expected, &equal, e))
        goto done;
    *owned = equal;
    ok = 1;
done:
    patch_context_close(&local);
    return ok;
}
/* Role values: runtime, sidecar, private original, receipt, legacy uninstaller,
   current uninstaller. These are the complete lifecycle path allowlist. */
static inline int medieval_path_role(const char *relative, size_t *file_index) {
    size_t i;
    char path[160];
    for (i = 0; i < MEDIEVAL_FILE_COUNT; ++i) {
        *file_index = i;
        if (!strcmp(relative, medieval_payload_names[i]))
            return 1;
        if (i == 6u) {
            if (!strcmp(relative, MEDIEVAL_STATE_NAME "/originals/CHANGES.TXT")) return 3;
            if (!strcmp(relative, "CHANGES.TXT.unofficial-patch.bak")) return 2;
            continue;
        }
        if (i == 7u || i == 8u || i == 9u) {
            const char *basename = i == 7u ? "DEFAULT_HEROES.TXT" :
                                   i == 8u ? "NAMES.TXT" : "DEFAULT_REGION_SPECIFIC.TXT";
            snprintf(path, sizeof(path), MEDIEVAL_STATE_NAME "/originals/%s", basename);
            if (!strcmp(relative, path)) return 3;
            snprintf(path, sizeof(path), "%s.unofficial-patch.bak", basename);
            if (!strcmp(relative, path)) return 2;
            continue;
        }
        snprintf(path, sizeof(path), "%s.unofficial-patch.bak", medieval_payload_names[i]);
        if (!strcmp(relative, path))
            return 2;
        snprintf(path, sizeof(path), MEDIEVAL_STATE_NAME "/originals/%s", medieval_payload_names[i]);
        if (!strcmp(relative, path))
            return 3;
    }
    *file_index = 0;
    if (!strcmp(relative, MEDIEVAL_STATE_NAME "/" MEDIEVAL_RECEIPT_NAME))
        return 4;
    if (!strcmp(relative, MEDIEVAL_STATE_NAME "/Uninstall.exe"))
        return 5;
    return !strcmp(relative, MEDIEVAL_UNINSTALL_NAME) ? 6 : 0;
}
static inline int medieval_registry_unknown_preserved(const PatchRegistry *registry,
                                                      const PatchRegistrySnapshot *before,
                                                      const PatchRegistrySnapshot *after, PatchError *e) {
    const JsonValue *v, *other;
    unsigned side;
    int owned;
    if (before->subkeys != after->subkeys)
        return json_invalid(e, "Registry subkeys would not be preserved.");
    for (side = 0; side < 2; ++side) {
        const PatchRegistrySnapshot *a = side ? after : before, *b = side ? before : after;
        for (v = a->values->child; v; v = v->next) {
            if (!patch_registry_owned_name(registry, json_get(v, "name")->string, &owned, e))
                return 0;
            if (!owned) {
                if (!patch_registry_find(registry, b, json_get(v, "name")->string, &other, e))
                    return 0;
                if (!patch_registry_same_data(v, other))
                    return json_invalid(e, "Unknown registry values would not be preserved.");
            }
        }
    }
    return 1;
}
static inline int medieval_journal_validate(JsonDocument *owner, const JsonValue *value,
                                            const MedievalIdentity *identity, MedievalJournal *out,
                                            PatchError *e) {
    MedievalJournal j = {0};
    const char *schema, *target, *directory, *sid, *kind;
    const JsonValue *actions, *a, *retained;
    size_t i = 0, k, index;
    int64_t started;
    unsigned required = 0;
    int role, restore, own, legacy, owned;
    REGSAM view, previous_view;
    PatchError nested = {0};
    memset(out, 0, sizeof(*out));
    schema = medieval_string(value, "schema", 0, e);
    if (!schema || strcmp(schema, MEDIEVAL_JOURNAL_SCHEMA) || !medieval_integrity_valid(owner, value, e))
        return json_invalid(e, "Invalid transaction schema or integrity.");
    target = medieval_string(value, "target", 0, e);
    directory = medieval_string(value, "directory_identity", 0, e);
    sid = medieval_string(value, "owner_sid", 0, e);
    j.operation = medieval_string(value, "operation", 0, e);
    j.phase = medieval_string(value, "phase", 0, e);
    j.installation_id = medieval_string(value, "installation_id", 0, e);
    actions = json_get(value, "actions");
    if (!target || !directory || !sid || !j.operation || !j.phase || !j.installation_id ||
        !medieval_integer(value, "started", 0, &started, e))
        return 0;
    restore = !strcmp(j.operation, "restore");
    if (strcmp(target, identity->target) || strcmp(directory, identity->guard->identity) ||
        strcmp(sid, identity->owner_sid) || (!restore && strcmp(j.operation, "install")) ||
        (strcmp(j.phase, "prepared") && strcmp(j.phase, "applying") && strcmp(j.phase, "committed") &&
         strcmp(j.phase, "rolled-back")) ||
        !actions || actions->type != JSON_ARRAY || actions->count < 8 || actions->count > 48 || started < 0 ||
        (uint64_t)started > actions->count || (!strcmp(j.phase, "prepared") && started) ||
        (!strcmp(j.phase, "committed") && (uint64_t)started != actions->count))
        return json_invalid(e, "Transaction belongs to another directory/account or has invalid progress.");
    retained = json_get(value, restore ? "removal_receipt" : "install_receipt");
    if (!medieval_receipt_validate(owner, retained, identity, 1, &j.receipt, &nested)) {
        if (nested.code)
            patch_error_set(e, nested.code, nested.message, nested.win32);
        return 0;
    }
    if (strcmp(j.installation_id, j.receipt.installation_id) || strcmp(j.receipt.status, "installed"))
        return json_invalid(e, "Transaction and retained receipt identify different installations.");
    /* PS v2 journals contain state_existed; old records missing it mean false,
       matching the original script/default-field semantics. */
    a = json_get(value, "state_existed");
    if (a && a->type != JSON_NULL) {
        if (a->type != JSON_BOOL)
            return json_invalid(e, "Invalid state existence flag.");
        j.state_existed = a->boolean;
    }
    if (!medieval_string(value, "recovery_archive", 1, e))
        return 0;
    for (a = actions->child; a; a = a->next, ++i) {
        MedievalAction *action = &j.actions[i];
        action->json = a;
        kind = medieval_string(a, "kind", 0, e);
        if (!kind)
            return 0;
        if (!strcmp(kind, "file")) {
            action->relative = medieval_string(a, "relative", 0, e);
            if (!action->relative)
                return 0;
            role = medieval_path_role(action->relative, &index);
            if (!role)
                return json_invalid(e, "Recovery file is outside the patch allowlist.");
            if ((role == 1 || role == 2 || role == 3) &&
                index >= medieval_receipt_file_count(&j.receipt))
                return json_invalid(e, "Recovery file is absent from the retained receipt.");
            for (k = 0; k < i; ++k)
                if (!j.actions[k].is_registry && !strcmp(j.actions[k].relative, action->relative))
                    return json_invalid(e, "Duplicate transaction file.");
            if (!medieval_file_record(json_get(a, "before"), &action->before_file, e) ||
                !medieval_file_record(json_get(a, "after"), &action->after_file, e))
                return 0;
            if (role == 1) {
                if (restore && (((index == 7u || index == 8u) &&
                                 !j.receipt.name_fix_enabled && !j.receipt.old_v6) ||
                                (index == 9u && !j.receipt.marriage_title_fix_enabled)))
                    return json_invalid(e, "Unselected game data cannot be a removal target.");
                const PatchFileRecord *expected =
                    restore ? &j.receipt.files[index].original : &j.receipt.files[index].installed;
                if (!patch_record_equal(&action->after_file, expected))
                    return json_invalid(e, "Runtime recovery differs from retained receipt.");
                required |= index >= 6u ? 1U << (index + 3u) : 1U << index;
            } else if (role == 6) {
                if (!action->after_file.exists ||
                    strcmp(action->after_file.sha256, j.receipt.uninstaller_sha256) ||
                    (restore && (!action->before_file.exists ||
                                 strcmp(action->before_file.sha256, j.receipt.uninstaller_sha256))))
                    return json_invalid(e, "Recovery uninstaller is not bound to receipt.");
                required |= 64;
            } else if (role == 4) {
                if (!action->after_file.exists || !action->after_file.length ||
                    (restore && !action->before_file.exists))
                    return json_invalid(e, "Incomplete recovery receipt action.");
                required |= 128;
            } else if (restore) {
                const MedievalFileState *f = &j.receipt.files[index];
                if (role != 2 || !f->sidecar_created ||
                    !patch_record_equal(&action->before_file, &f->original) || action->after_file.exists)
                    return json_invalid(e, "Unowned removal sidecar action.");
            } else if (role == 2) {
                const MedievalFileState *f = &j.receipt.files[index];
                if (!f->sidecar_created || action->before_file.exists ||
                    !patch_record_equal(&action->after_file, &f->original))
                    return json_invalid(e, "Unowned installation sidecar action.");
            } else if (role == 3) {
                const MedievalFileState *f = &j.receipt.files[index];
                if (action->after_file.exists) {
                    if (!patch_record_equal(&action->after_file, &f->original))
                        return json_invalid(e, "Private original differs from retained receipt.");
                } else if (!*f->legacy_original_sha256 || !action->before_file.exists ||
                           strcmp(action->before_file.sha256, f->legacy_original_sha256))
                    return json_invalid(e, "Unowned private original retirement.");
            } else if (role == 5 && action->after_file.exists)
                return json_invalid(e, "Legacy uninstaller cannot be introduced.");
        } else if (!strcmp(kind, "registry")) {
            action->is_registry = 1;
            action->hive = medieval_string(a, "hive", 0, e);
            action->view = medieval_string(a, "view", 0, e);
            action->name = medieval_string(a, "name", 0, e);
            if (!action->hive || !action->view || !action->name ||
                !patch_registry_view(&identity->registry, action->view, &view, e))
                return 0;
            own = !strcmp(action->hive, "CurrentUser") && !strcmp(action->view, "Registry32") &&
                  !strcmp(action->name, identity->registration_key);
            legacy = (!strcmp(action->hive, "CurrentUser") || !strcmp(action->hive, "LocalMachine")) &&
                     (!strcmp(action->name, patch_legacy_registry_names[0]) ||
                      !strcmp(action->name, patch_legacy_registry_names[1]));
            if (!own && !legacy)
                return json_invalid(e, "Recovery names an unrelated registry key.");
            for (k = 0; k < i; ++k)
                if (j.actions[k].is_registry && !strcmp(j.actions[k].hive, action->hive) &&
                    !strcmp(j.actions[k].name, action->name)) {
                    if (!patch_registry_view(&identity->registry, j.actions[k].view, &previous_view, e))
                        return 0;
                    if (view == previous_view || !strcmp(action->hive, "CurrentUser"))
                        return json_invalid(e, "Duplicate physical registry action.");
                }
            if (!patch_registry_validate(&identity->registry, json_get(a, "before"), &action->before_registry,
                                         e) ||
                !patch_registry_validate(&identity->registry, json_get(a, "after"), &action->after_registry,
                                         e))
                return 0;
            if (own && !medieval_registry_ownership(identity, &action->before_registry, j.installation_id, e))
                return 0;
            if (!medieval_registry_unknown_preserved(&identity->registry, &action->before_registry,
                                                     &action->after_registry, e))
                return 0;
            if (restore || legacy) {
                const JsonValue *v;
                if (legacy && restore)
                    return json_invalid(e, "Legacy removal only belongs to migration.");
                for (v = action->after_registry.values->child; v; v = v->next) {
                    if (!patch_registry_owned_name(&identity->registry, json_get(v, "name")->string, &owned,
                                                   e))
                        return 0;
                    if (owned)
                        return json_invalid(e, "Registry removal retains owned values.");
                }
                if (action->after_registry.exists !=
                    (action->after_registry.values->count > 0 || action->after_registry.subkeys > 0))
                    return json_invalid(e, "Invalid removed registry existence.");
            } else if (!medieval_registry_installed_identity(identity, &action->after_registry,
                                                              j.installation_id, e))
                return json_invalid(e, "Invalid installed registration identity.");
            if (legacy && action->before_registry.exists) {
                if (!medieval_legacy_owned(identity, &action->before_registry, &owned, e))
                    return 0;
                if (!owned)
                    return json_invalid(e, "Legacy registration does not belong to this folder.");
            }
            if (own)
                required |= 256;
        } else
            return json_invalid(e, "Unknown transaction action.");
    }
    {
        unsigned base = j.receipt.old_v2 || j.receipt.legacy ? 479U :
                        j.receipt.old_v3 || j.receipt.old_v4 ? 511U : 1023U;
        unsigned name_bits = medieval_receipt_file_count(&j.receipt) < 9u ||
            (!j.receipt.name_fix_enabled && !j.receipt.old_v6) ? 0U : 3072U;
        unsigned marriage_bits = j.receipt.marriage_title_fix_enabled ? 4096U : 0U;
        unsigned expected = base | name_bits | marriage_bits;
        unsigned optional_install_bits = !restore ?
            ((medieval_receipt_file_count(&j.receipt) >= 9u &&
              !j.receipt.name_fix_enabled && !j.receipt.old_v6) ? 3072U : 0U) |
            (!j.receipt.marriage_title_fix_enabled && !j.receipt.old_v7 &&
             !j.receipt.old_v6 && !j.receipt.old_v5 && !j.receipt.old_v4 &&
             !j.receipt.old_v3 && !j.receipt.old_v2 && !j.receipt.legacy ? 4096U : 0U) : 0U;
        if ((required & expected) != expected || (required & ~(expected | optional_install_bits)) ||
            ((required & 3072U) != 0U && (required & 3072U) != 3072U))
            return json_invalid(e, "Recovery omits a required runtime, receipt, uninstaller or registration.");
    }
    j.json = value;
    j.count = actions->count;
    j.started = (size_t)started;
    *out = j;
    return 1;
}
static inline int medieval_journal_verify_copies(const MedievalIdentity *identity,
                                                 const MedievalJournal *journal, const wchar_t *transaction,
                                                 PatchError *e) {
    PatchContext local = {0};
    PatchFileRecord actual;
    size_t i;
    unsigned side;
    char relative[32];
    wchar_t *path = NULL;
    int ok = 0;
    if (!strcmp(journal->phase, "committed") || !strcmp(journal->phase, "rolled-back"))
        return 1;
    local.allocator = identity->context.allocator;
    local.memory_limit = identity->context.memory_limit;
    for (i = 0; i < journal->count; ++i)
        if (!journal->actions[i].is_registry)
            for (side = 0; side < 2; ++side) {
                const PatchFileRecord *expected =
                    side ? &journal->actions[i].after_file : &journal->actions[i].before_file;
                if (!expected->exists)
                    continue;
                snprintf(relative, sizeof(relative), "%s/%03u.bin", side ? "after" : "before", (unsigned)i);
                if (!patch_path_join(&local, transaction, relative, &path, e) ||
                    !patch_file_record(identity->guard, path, &actual, e))
                    goto done;
                if (!patch_record_equal(&actual, expected)) {
                    json_invalid(e, "Missing or damaged transaction copy.");
                    goto done;
                }
                patch_context_free(&local, path);
                path = NULL;
            }
    ok = 1;
done:
    patch_context_close(&local);
    return ok;
}
static inline int medieval_old_patch(const char *mode) {
    if (!strcmp(mode, "r292-dust-alpha") || !strcmp(mode, "r292-credits-old")) return 1;
    return !strcmp(mode, "r291-deployment-stage-sync") || !strcmp(mode, "r291-credits-old") || !strcmp(mode, "r290-zoom-tooltip") || !strcmp(mode, "r290-credits-old") || !strcmp(mode, "r289-persistent-zoom") || !strcmp(mode, "r289-credits-old") || !strcmp(mode, "r288-key-customisation") || !strcmp(mode, "r288-locale-old") || !strcmp(mode, "r288-credits-old") || !strcmp(mode, "r237-key-customisation") || !strcmp(mode, "r287-credits") || !strcmp(mode, "r205-key-reject") || !strcmp(mode, "r204-keytable") || !strcmp(mode, "r203-zoom") || !strcmp(mode, "r202") || !strcmp(mode, "r201") || !strcmp(mode, "r200") || !strcmp(mode, "r199") || !strcmp(mode, "r198") || !strcmp(mode, "r197") || !strcmp(mode, "r196") || !strcmp(mode, "r195") || !strcmp(mode, "r194") || !strcmp(mode, "r191") || !strcmp(mode, "r189") || !strcmp(mode, "r188") || !strcmp(mode, "r187") || !strcmp(mode, "r186") || !strcmp(mode, "r185") ||
           !strcmp(mode, "dust-only") || !strcmp(mode, "r6f160");
}
static inline int medieval_old_configuration(const char *hash) {
    return !strcmp(hash, "23A43425ADBA421BAF9531220E75964F59E829F67CE8577BDE1C45EFBCAD61DA") ||
           !strcmp(hash, "BD21E07D4B9282A8CA0F53613CCB852D419E5D54967A96C8E34A60D2F96E476A");
}
static inline const char *medieval_known_d3d9_mode(const char *hash) {
    if (mtw_credits_proxy_known_hash(hash))
        return "r293-credits";
    if (mtw_runtime_proxy_known_hash(hash)) return "r293-key-text";
    if (mtw_r292_credits_proxy_known_hash(hash)) return "r292-credits-old";
    if (mtw_r291_credits_proxy_known_hash(hash)) return "r291-credits-old";
    if (mtw_r290_credits_proxy_known_hash(hash)) return "r290-credits-old";
    if (mtw_r289_credits_proxy_known_hash(hash)) return "r289-credits-old";
    if (mtw_r288_credits_proxy_known_hash(hash)) return "r288-credits-old";
    if (mtw_previous_credits_proxy_known_hash(hash)) return "r287-credits";
    static const struct {
        const char *hash, *mode;
    } known[] = {{"E36F5C8140EB6D1DC8F35E60AB231C07DFA2EB667F9CC0A909AC2D419DE078C6", "stock-dgvoodoo"},
                 {"34F855A17C10B6BBCEFD844B99A5B5A7F442DECDDFFDB0D898645FA2FF0B0F0C", "dust-only"},
                 {"E421B9A1FF5927A8B93FD7B2A83E0C965EB1EA596D924561713C16F323924892", "r6f160"},
                 {"A3FFCC0BCDD74044448BF0418F0FA732594F23D025DE667172AFE118CD82F9FA", "r185"},
                 {"3EE7EE33946F9F73A61559C23505AFCC27D45E61067644AF611B09F627297AD8", "r185"},
                 {"060136A2BE50A0F209FF74F242FFC264C6709C48135C8EB362771DCBF25931F1", "r185"},
                 {"C0D597364734EAEA26ABA83F1FA6B8875B830E4D626D5CF41F42DAEB92106CB4", "r185"},
                 {"9D1C8B4E5C0CF0A224FFEB8C20F762C52B1D8C2B306162A639D81295B103D04E", "r185"},
                 {"E3D5D6D5E214D79592B7D6F7F26D52CDFF5A59EB028CCD9DE499900BCFF18D74", "r185"},
                 {"F5F7EEDB312D251ECE1CCC726A08E866020C89A3C00212216B6A70FC0F5D6BE0", "r185"},
                 {"CC3537E286863FA75200DFB80839F07E07D8AD91F2ABBB2A4C5629677CE965FD", "r185"},
                 {"8FD9B9CE5809C52E2C815754ECD391D6ACDA14D33518A70BA9B4BE1FFD5DC7DF", "r185"},
                 {"D61A5DB23EE091CAE0D97AB5E385D42BD301E97D7F9A4FB6F3A3CA1484E7B932", "r185"},
                 {"E2B6F73CCA21467E14505AFF8DA31B8454D4D7546C499285670ED6A2FF4E0DF5", "r185"},
                 {"3A11DF858C9B1339297B5E78684C07722DB6A2A3C378173FEE8E640B19F53F67", "r185"},
                 {"B7F1FEA6588BDED9270E6A51E1CA80FE20F1F32A87112101212A8D476287384F", "r185"},
                 {"E5703EA5E8B5F33F8CD6F41B50125D2187F73C50DA3DC8F621F4E160D28B4BC9", "r185"},
                 {"FD1271790E739C86D29E4EE776A4621A03236192AB436AE24B0836B26B423E4C", "r185"},
                 {"EDF4B6CB4F2DEF7563E15AF46AFE3A8708F240B6FE1EC174D14DC2FAA61D49A3", "r185"},
                 {"B07D861994FBDEC15956BFF5FB79882A9D2E630CF72ECDA07BC3EBE7E5248875", "r185"},
                 {"516D61823F0629856199C0E6F888F4CF803B711CC569058D0D88D0D72F0C18FA", "r185"},
                 {"48ED6EF8DB1197A0E1240CB73B2217F66CA863F7A4E4568DB00ADD4E4288F6CF", "r185"},
                 {"CBB6A16CE535640B4FDB6526F42E575EF882E4CFE232BA8CF8BAAF8735E8596A", "r185"},
                 {"9791F095DB66817A4CB6A6B23DF4983254950F7F1266D1CDC3040E1E9FEFDE4C", "r186"},
                 {"AD7E922E1F160C045325E75107E507E54807F426BFD8102A1808E969AD67CFCA", "r186"},
                 {"24E0C23B0C1424F77201A83D449D22165694D6D3535D187BC9FD3247DC2A8F0E", "r186"},
                 {"5E0E398BB5D10F2855928844A374E966FCDC71D28EAAA0D70BA17FA7ADE8838C", "r186"},
                 {"300373700D0868CF2B1BA94762132A781E70918A01DB873DA3E8666FCD73B8F1", "r186"},
                 {"7249A0118DAE0DAB81E2E72538F75555E09B1B72EFA6F2AFFF4E6E56812E97A8", "r187"},
                 {"9E19B39293D61D1B4A513F1EB1EDC958A5153123741F37D28B126337CD7C59A4", "r187"},
                 {"FE750F1D73D8FD4D9BA6BFB17B3B9F22ABB817DD1610C17EF2860DA7BF8BCE08", "r187"},
                 {"064C234EB46ABDC690F42C5AFE0BFBC61F2D388DE63F9F28D23A27E064E0D6A7", "r187"},
                 {"BFCE156546CF8624BD48170D56DA7AF8760E3BCFDB7944DE6888959BA87E91EA", "r188"},
                 {"037FDF75724D5698A0C2A41DE66508C8288B470E464D5FF721ABA77CDAFBC7BC", "r188"},
                 {"A57B63E4402124304B02A45305739D2606AAE7585C7395ED7F8FE77E209F78F0", "r188"},
                 {"850861A4C92211099656ED76B06B8C443CEF162FEDD23AB6DE70DD7181704EDB", "r188"},
                 {"D9C4A7D44A8DA738A25B85F4D485AE6A3E6BE2CF10D455734A2216E320F36DFC", "r189"},
                 {"11169906D379219A80B3D3076A679656951B0E2A91FCD29D2AF53DD4DDA6814B", "r189"},
                 {"30A915109049D7083EEA6E066A92AE327BC1A0C50186DB7F476393A3D26570E9", "r189"},
                 {"09333B64AB577BD0DBFAFC5ACC26A8E19AFA82556FED3C414D3433B22315A243", "r189"},
                 {"A9CB1122929A57447719875EEF697719616003BF6B2A4E57B633B6DBF006F21E", "r191"},
                 {"8D035A927079E8555672258C614C18876F669861A9A03859E7394A54DF41D0E9", "r191"},
                 {"74813C992BDA3C877FB929168B2E65DFAA87C12EC1C7CAFD81A3343D93666074", "r191"},
                 {"F8883FDECB59BD0A57B0C1B6D446A3A096F9ACF89097B784A1189F51210CE551", "r191"},
                 {"F5F89A35D8504CC2157A5158AFBE5E85ACFCD98A992F6538C38BDE40D0CE60FE", "r194"},
                 {"8E02E01E38E649DD2B3A9F14E59C6D76958F08884DD43099A703BBC0A74DB8A5", "r194"},
                 {"C10D1AC084C7469BBF18B92C3B7A96274971A39505524D0E30DB86CAAB616EFA", "r194"},
                 {"2E0680889E855FB2E5931DC7D0971B875196E8E05231E926BF0DDA42C8FAF7FE", "r194"},
                 {"BC7A0D30EF28E01715ABC05F1C298D884767760CA193B4171DF2A14FB2D4C848", "r195"},
                 {"DAC23D50C2A024084A354527D882BE0E04BF2BDBBDA66E9CC81017D31B3C963A", "r195"},
                 {"397AB7FC769D8DBA919DE2E62A7F5E0A3A32870B5733C3DCD5D43BF472AC6232", "r195"},
                 {"B620EFA4334DA6D8317DD7EAEC22805F04C16A01BC947373271B7E89635EC0A9", "r195"},
                 {MEDIEVAL_R196_FAST_ONLY_PROXY_HASH, "r196"},
                 {MEDIEVAL_R196_TERRAIN_FAST_PROXY_HASH, "r196"},
                 {"C94CB5D586F68D6132A4B9F49F1D16D83EEB7ACCC6201F84CED0FCADC32F1244", "r197"},
                 {"A5937F08664AC14F79BDCD97002C20F37B53D748E126E9CDFC410450436BCC23", "r197"},
                 {"84C3EF9423EFBDBAC79ADD53E3AF61D402DB3BBBCB287628F5F79FD90856611B", "r197"},
                 {MEDIEVAL_R197_SPRITE_DISABLED_PROXY_HASH, "r197"},
                 {MEDIEVAL_R199_FAST_ONLY_PROXY_HASH, "r197"},
                 {MEDIEVAL_R197_TERRAIN_FAST_PROXY_HASH, "r197"},
                 {"480FCA4E2B2E182D1F7E03C1BCE32A691F6A7F24D55C9C04D625939EBF326A72", "r198"},
                 {"D842059D8982304E8D602EC79F7330852FFD6B9EB4C65143161A80C16555F341", "r198"},
                 {"43CCB84C1452E39AFA4427DC8F9442228D1A41A6F1A0A0B5F0883F058D997CC0", "r198"},
                 {MEDIEVAL_R198_SPRITE_DISABLED_PROXY_HASH, "r198"},
                 {MEDIEVAL_R198_TERRAIN_FAST_PROXY_HASH, "r198"},
                 {"926E7A23902AC60CC71EB130EBFD93F723D3F2B52BAAA4FBBD667EF3C7E42CF3", "r200"},
                 {"AB5623557CE8AAD6B62DE13C85355AB30943EBC9117D41630CA805E40670110B", "r200"},
                 {"284B30F025F36F8422B6BE20ACED5A7B9B7590753E5F8EE68FC95D0BF9CCBAF3", "r200"},
                 {MEDIEVAL_SPRITE_DISABLED_PROXY_HASH, "r200"},
                 {MEDIEVAL_FAST_ONLY_PROXY_HASH, "r200"},
                 {MEDIEVAL_TERRAIN_FAST_PROXY_HASH, "r200"},
                 {"CF8E95967509CE9A0BEE4368046F4F93E7C9A7B907B9A3E3EDE1BCEDC942EFDC", "r199"},
                 {"352DB446067CCBC6BE8D2274F8EC81E1436F34EA5520D4FF9CB388BE17BA438F", "r199"},
                 {"DEE393BDEE63F429BCDBBD92EED0741A6401099224506A3803BB42B49AF1B874", "r199"},
                 {MEDIEVAL_R199_SPRITE_DISABLED_PROXY_HASH, "r199"},
                 {MEDIEVAL_R199_FAST_ONLY_PROXY_HASH, "r199"},
                 {MEDIEVAL_R199_TERRAIN_FAST_PROXY_HASH, "r199"}};
    static const char *const r202[] = {
        MEDIEVAL_R202_TERRAIN_ICON_HASH, MEDIEVAL_R202_TERRAIN_FAST_ICON_HASH,
        MEDIEVAL_R202_TERRAIN_NO_ICON_HASH, MEDIEVAL_R202_TERRAIN_FAST_NO_ICON_HASH,
        MEDIEVAL_R202_ICON_ONLY_HASH, MEDIEVAL_R202_FAST_ICON_HASH,
        MEDIEVAL_R202_FAST_ONLY_PROXY_HASH};
    static const char *const r203_zoom[] = {
        MEDIEVAL_R203_TERRAIN_ICON_ZOOM_HASH, MEDIEVAL_R203_TERRAIN_FAST_ICON_ZOOM_HASH,
        MEDIEVAL_R203_TERRAIN_NO_ICON_ZOOM_HASH, MEDIEVAL_R203_TERRAIN_FAST_NO_ICON_ZOOM_HASH,
        MEDIEVAL_R203_ZOOM_ONLY_HASH, MEDIEVAL_R203_FAST_ZOOM_HASH,
        MEDIEVAL_R203_ICON_ZOOM_HASH, MEDIEVAL_R203_FAST_ICON_ZOOM_HASH};
    static const char *const r288[] = {
        MEDIEVAL_R288_TERRAIN_ICON_HASH, MEDIEVAL_R288_TERRAIN_FAST_ICON_HASH,
        MEDIEVAL_R288_TERRAIN_NO_ICON_HASH, MEDIEVAL_R288_TERRAIN_FAST_NO_ICON_HASH,
        MEDIEVAL_R288_FAST_ONLY_HASH, MEDIEVAL_R288_ICON_ONLY_HASH, MEDIEVAL_R288_FAST_ICON_HASH,
        MEDIEVAL_R288_TERRAIN_ICON_ZOOM_HASH, MEDIEVAL_R288_TERRAIN_FAST_ICON_ZOOM_HASH,
        MEDIEVAL_R288_TERRAIN_NO_ICON_ZOOM_HASH, MEDIEVAL_R288_TERRAIN_FAST_NO_ICON_ZOOM_HASH,
        MEDIEVAL_R288_ZOOM_ONLY_HASH, MEDIEVAL_R288_FAST_ZOOM_HASH,
        MEDIEVAL_R288_ICON_ZOOM_HASH, MEDIEVAL_R288_FAST_ICON_ZOOM_HASH};
    static const char *const r289[] = {
        MEDIEVAL_R289_TERRAIN_FAST_ICON_HASH, MEDIEVAL_R289_TERRAIN_FAST_NO_ICON_HASH,
        MEDIEVAL_R289_FAST_ONLY_HASH, MEDIEVAL_R289_FAST_ICON_HASH,
        MEDIEVAL_R289_TERRAIN_ICON_ZOOM_HASH, MEDIEVAL_R289_TERRAIN_FAST_ICON_ZOOM_HASH,
        MEDIEVAL_R289_TERRAIN_NO_ICON_ZOOM_HASH, MEDIEVAL_R289_TERRAIN_FAST_NO_ICON_ZOOM_HASH,
        MEDIEVAL_R289_ZOOM_ONLY_HASH, MEDIEVAL_R289_FAST_ZOOM_HASH,
        MEDIEVAL_R289_ICON_ZOOM_HASH, MEDIEVAL_R289_FAST_ICON_ZOOM_HASH};
    static const char *const r290[] = {
        MEDIEVAL_R290_TERRAIN_FAST_ICON_HASH, MEDIEVAL_R290_TERRAIN_FAST_NO_ICON_HASH,
        MEDIEVAL_R290_FAST_ONLY_HASH, MEDIEVAL_R290_FAST_ICON_HASH,
        MEDIEVAL_R290_TERRAIN_ICON_ZOOM_HASH, MEDIEVAL_R290_TERRAIN_FAST_ICON_ZOOM_HASH,
        MEDIEVAL_R290_TERRAIN_NO_ICON_ZOOM_HASH, MEDIEVAL_R290_TERRAIN_FAST_NO_ICON_ZOOM_HASH,
        MEDIEVAL_R290_ZOOM_ONLY_HASH, MEDIEVAL_R290_FAST_ZOOM_HASH,
        MEDIEVAL_R290_ICON_ZOOM_HASH, MEDIEVAL_R290_FAST_ICON_ZOOM_HASH};
    static const char *const r291[] = {
        MEDIEVAL_R291_TERRAIN_ICON_HASH, MEDIEVAL_R291_TERRAIN_FAST_ICON_HASH,
        MEDIEVAL_R291_TERRAIN_NO_ICON_HASH, MEDIEVAL_R291_TERRAIN_FAST_NO_ICON_HASH,
        MEDIEVAL_R291_TERRAIN_ICON_ZOOM_HASH, MEDIEVAL_R291_TERRAIN_FAST_ICON_ZOOM_HASH,
        MEDIEVAL_R291_TERRAIN_NO_ICON_ZOOM_HASH, MEDIEVAL_R291_TERRAIN_FAST_NO_ICON_ZOOM_HASH};
    static const char *const r292[] = {
        MEDIEVAL_R292_TERRAIN_ICON_HASH, MEDIEVAL_R292_TERRAIN_FAST_ICON_HASH,
        MEDIEVAL_R292_TERRAIN_NO_ICON_HASH, MEDIEVAL_R292_TERRAIN_FAST_NO_ICON_HASH,
        MEDIEVAL_R292_TERRAIN_ICON_ZOOM_HASH, MEDIEVAL_R292_TERRAIN_FAST_ICON_ZOOM_HASH,
        MEDIEVAL_R292_TERRAIN_NO_ICON_ZOOM_HASH, MEDIEVAL_R292_TERRAIN_FAST_NO_ICON_ZOOM_HASH};
    static const char *const r288_locale_old[] = {
        MEDIEVAL_R288_TERRAIN_FAST_ICON_HASH, MEDIEVAL_R288_TERRAIN_FAST_NO_ICON_HASH,
        MEDIEVAL_R288_FAST_ONLY_HASH, MEDIEVAL_R288_FAST_ICON_HASH,
        MEDIEVAL_R288_TERRAIN_ICON_ZOOM_HASH, MEDIEVAL_R288_TERRAIN_FAST_ICON_ZOOM_HASH,
        MEDIEVAL_R288_TERRAIN_NO_ICON_ZOOM_HASH, MEDIEVAL_R288_TERRAIN_FAST_NO_ICON_ZOOM_HASH,
        MEDIEVAL_R288_ZOOM_ONLY_HASH, MEDIEVAL_R288_FAST_ZOOM_HASH,
        MEDIEVAL_R288_ICON_ZOOM_HASH, MEDIEVAL_R288_FAST_ICON_ZOOM_HASH};
    /* R237 staged installations remain recognisable during a local upgrade. */
    static const char *const r237_key[] = {
        "AADC364E2C602B82586ED6328E6CA4DA40167E1F031F74941F462BEE68C046F2",
        "742B78D606F6E9A8B19FE71A5CF64937B2647ACCC838FC7B9710E37A7E2F658D",
        "C1C50874B12D29ED15E52DEFE89EEE4D60C19F080408B0D0DF174CE5BD3D2B1B",
        "01731B610B20FBF321713A41A0324F80AF31121F82EC7959BE2830A2CE1DBC57",
        "0323B0CE2CE7BCA8C337CDC96D320A54C05E8688E6251E2626B6859D6A72115F",
        "AA49FFD6734228CD4A4888B0394A013EA973FE7019A21D2E45060232F040AC42",
        "F5B9775E476F0A7CCEE85E067A554049782D89C68A5B360B1A425720DB368CB2",
        "C504854F89AE61E73F5319F36D7D1AF0EBAFC5489AD9416F75B490D85B7C0EFC",
        "364EE732D99B534CCEB4DF0C98C0AAF58C928F022C2652B19F796D7AED257163",
        "1439248C821ACE90C2206DCBBAC9DB7699C5152BF4DB652EB6E18E8E97793A1A",
        "A3D1B375E009E6E8879B714A7B710978BDAFF00B8F2120E68FC9C01D7BC6BA35",
        "2FA16CE6FED6C2A7F9E0DEEC0A494F5E9540AE7F30E3DD35202031D7439F1632",
        "7F732D532535580931C137B7D58226C546FD566043A8BD27FB92C25C79523ABD",
        "2992FAEEBC6B9599823FB20460BD7A6B4FCEE787EA6749BA96422871958B4993",
        "90FD779BD57C941DA50490E9AD9EC95F241266793B87359E71C017F51582A196"};
    static const char *const r204_keytable[] = {
        MEDIEVAL_R204_TERRAIN_ICON_HASH, MEDIEVAL_R204_TERRAIN_FAST_ICON_HASH,
        MEDIEVAL_R204_TERRAIN_NO_ICON_HASH, MEDIEVAL_R204_TERRAIN_FAST_NO_ICON_HASH,
        MEDIEVAL_R204_FAST_ONLY_HASH, MEDIEVAL_R204_FAST_ICON_HASH,
        MEDIEVAL_R204_TERRAIN_ICON_ZOOM_HASH, MEDIEVAL_R204_TERRAIN_FAST_ICON_ZOOM_HASH,
        MEDIEVAL_R204_TERRAIN_NO_ICON_ZOOM_HASH, MEDIEVAL_R204_TERRAIN_FAST_NO_ICON_ZOOM_HASH,
        MEDIEVAL_R204_ZOOM_ONLY_HASH, MEDIEVAL_R204_FAST_ZOOM_HASH,
        MEDIEVAL_R204_ICON_ZOOM_HASH, MEDIEVAL_R204_FAST_ICON_ZOOM_HASH};
    static const char *const r205_key_reject[] = {
        MEDIEVAL_R205_TERRAIN_ICON_HASH, MEDIEVAL_R205_TERRAIN_FAST_ICON_HASH,
        MEDIEVAL_R205_TERRAIN_NO_ICON_HASH, MEDIEVAL_R205_TERRAIN_FAST_NO_ICON_HASH,
        MEDIEVAL_R205_FAST_ONLY_HASH, MEDIEVAL_R205_FAST_ICON_HASH,
        MEDIEVAL_R205_ICON_ONLY_HASH,
        MEDIEVAL_R205_TERRAIN_ICON_ZOOM_HASH, MEDIEVAL_R205_TERRAIN_FAST_ICON_ZOOM_HASH,
        MEDIEVAL_R205_TERRAIN_NO_ICON_ZOOM_HASH, MEDIEVAL_R205_TERRAIN_FAST_NO_ICON_ZOOM_HASH,
        MEDIEVAL_R205_ZOOM_ONLY_HASH, MEDIEVAL_R205_FAST_ZOOM_HASH,
        MEDIEVAL_R205_ICON_ZOOM_HASH, MEDIEVAL_R205_FAST_ICON_ZOOM_HASH};
    static const char *const r201[] = {
        MEDIEVAL_R201_TERRAIN_ICON_HASH, MEDIEVAL_R201_TERRAIN_FAST_ICON_HASH,
        MEDIEVAL_R201_TERRAIN_NO_ICON_HASH, MEDIEVAL_R201_TERRAIN_FAST_NO_ICON_HASH,
        MEDIEVAL_R201_ICON_ONLY_HASH, MEDIEVAL_R201_FAST_ICON_HASH};
    size_t i;
    for (i = 0; i < sizeof(r292) / sizeof(*r292); ++i)
        if (!strcmp(hash, r292[i])) return "r292-dust-alpha";
    for (i = 0; i < sizeof(r291) / sizeof(*r291); ++i)
        if (!strcmp(hash, r291[i])) return "r291-deployment-stage-sync";
    for (i = 0; i < sizeof(r290) / sizeof(*r290); ++i)
        if (!strcmp(hash, r290[i])) return "r290-zoom-tooltip";
    for (i = 0; i < sizeof(r289) / sizeof(*r289); ++i)
        if (!strcmp(hash, r289[i])) return "r289-persistent-zoom";
    for (i = 0; i < sizeof(r288_locale_old) / sizeof(*r288_locale_old); ++i)
        if (!strcmp(hash, r288_locale_old[i])) return "r288-locale-old";
    for (i = 0; i < sizeof(r288) / sizeof(*r288); ++i)
        if (!strcmp(hash, r288[i])) return "r288-key-customisation";
    for (i = 0; i < sizeof(r237_key) / sizeof(*r237_key); ++i)
        if (!strcmp(hash, r237_key[i])) return "r237-key-customisation";
    for (i = 0; i < sizeof(r205_key_reject) / sizeof(*r205_key_reject); ++i)
        if (!strcmp(hash, r205_key_reject[i])) return "r205-key-reject";
    for (i = 0; i < sizeof(r204_keytable) / sizeof(*r204_keytable); ++i)
        if (!strcmp(hash, r204_keytable[i])) return "r204-keytable";
    for (i = 0; i < sizeof(r203_zoom) / sizeof(*r203_zoom); ++i)
        if (!strcmp(hash, r203_zoom[i])) return "r203-zoom";
    for (i = 0; i < sizeof(r202) / sizeof(*r202); ++i)
        if (!strcmp(hash, r202[i])) return "r202";
    for (i = 0; i < sizeof(r201) / sizeof(*r201); ++i)
        if (!strcmp(hash, r201[i])) return "r201";
    for (i = 0; i < sizeof(known) / sizeof(*known); ++i)
        if (!strcmp(hash, known[i].hash))
            return known[i].mode;
    return "";
}
static inline int medieval_known_old_runtime(const char *name, const char *hash, const char *payload_hash) {
    if (!strcmp(name, "D3D9.dll"))
        return medieval_old_patch(medieval_known_d3d9_mode(hash));
    return !strcmp(hash, payload_hash) ||
           (!strcmp(name, "dgVoodoo.conf") && medieval_old_configuration(hash));
}
typedef struct {
    int preserve_current, archive_current;
    const char *baseline_kind;
} MedievalBaselineDecision;
static inline MedievalBaselineDecision medieval_baseline_decision(const char *name,
                                                                  const PatchFileRecord *current,
                                                                  const char *mode,
                                                                  const char *payload_hash) {
    MedievalBaselineDecision result = {current->exists, 0, "verified-preinstall"};
    if (medieval_old_patch(mode) && current->exists) {
        result.archive_current = 1;
        if (strcmp(name, "dgVoodoo.conf") || !strcmp(current->sha256, payload_hash) ||
            medieval_old_configuration(current->sha256)) {
            result.preserve_current = 0;
            result.baseline_kind = "older-patch-archived";
        }
    }
    return result;
}
/* Draft creation preserves ordered members and the earliest baseline; it is
   deliberately unsealed until Task 4 fills files/completed_utc/archive fields.
   new_guid and utc are supplied by the lifecycle clock/UUID owner. */
static inline int medieval_receipt_draft(JsonDocument *doc, const MedievalIdentity *identity,
                                         const MedievalReceipt *previous, const char *mode,
                                         const char *uninstaller_hash, const char *version,
                                         const char *new_guid, const char *utc,
                                         const JsonValue *locked_settings, const char *installer_hash,
                                         JsonValue **out, PatchError *e) {
    JsonValue *r = NULL, *locked;
    const char *restoration;
    int equal;
    int64_t count;
    *out = NULL;
    if (!medieval_hash_valid(uninstaller_hash) || !medieval_guid_valid(new_guid) ||
        (installer_hash && !medieval_hash_valid(installer_hash)))
        return json_invalid(e, "Invalid receipt draft identity.");
#define MS_TEXT(k, s)                                                                                        \
    do {                                                                                                     \
        if (!json_set(doc, r, (k), json_text(doc, (s), e), e))                                               \
            return 0;                                                                                        \
    } while (0)
#define MS_VALUE(k, v)                                                                                       \
    do {                                                                                                     \
        if (!json_set(doc, r, (k), (v), e))                                                                  \
            return 0;                                                                                        \
    } while (0)
    if (previous) {
        r = json_clone(doc, previous->json, e);
        if (!r)
            return 0;
        if (!medieval_integer(previous->json, "repair_count", 1, &count, e))
            return 0;
        if (count < 0 || count == INT64_MAX)
            return json_invalid(e, "Repair count exceeds supported range.");
        MS_VALUE("repair_count", json_number(doc, count + 1, e));
        MS_TEXT("last_repaired_utc", utc);
        if (previous->legacy) {
            if (!medieval_text_path_equal(identity, previous->target_directory, identity->target, &equal, e))
                return 0;
            if (!equal) {
                MS_TEXT("legacy_installation_id", previous->installation_id);
                MS_TEXT("legacy_target_directory", previous->target_directory);
                MS_TEXT("installation_id", new_guid);
            }
        }
    } else {
        r = json_new(doc, JSON_OBJECT, e);
        if (!r)
            return 0;
        MS_TEXT("schema", MEDIEVAL_RECEIPT_SCHEMA);
        MS_TEXT("status", "installed");
        MS_TEXT("installation_id", new_guid);
        MS_TEXT("installer_version", version);
        MS_VALUE("installer_sha256", json_new(doc, JSON_NULL, e));
        MS_TEXT("installed_utc", utc);
        MS_TEXT("target_directory", identity->target);
        MS_TEXT("target_executable_sha256", MEDIEVAL_EXECUTABLE_HASH);
        MS_TEXT("preinstall_mode", mode);
        MS_VALUE("repair_count", json_number(doc, 0, e));
        MS_VALUE("files", json_new(doc, JSON_OBJECT, e));
        locked = locked_settings ? json_clone(doc, locked_settings, e) : json_new(doc, JSON_NULL, e);
        MS_VALUE("locked_settings", locked);
    }
    MS_TEXT("schema", MEDIEVAL_RECEIPT_SCHEMA);
    MS_TEXT("status", "installed");
    MS_TEXT("target_directory", identity->target);
    MS_TEXT("directory_identity", identity->guard->identity);
    MS_TEXT("owner_sid", identity->owner_sid);
    MS_TEXT("registration_key", identity->registration_key);
    MS_TEXT("uninstaller_sha256", uninstaller_hash);
    MS_TEXT("installer_version", version);
    locked = locked_settings ? json_clone(doc, locked_settings, e) : json_new(doc, JSON_NULL, e);
    MS_VALUE("locked_settings", locked);
    restoration = medieval_string(r, "restoration_kind", 1, e);
    if (!restoration)
        return 0;
    MS_TEXT("restoration_kind", *restoration ? restoration : "verified-preinstall");
    if (installer_hash)
        MS_TEXT("installer_sha256", installer_hash);
#undef MS_TEXT
#undef MS_VALUE
    *out = r;
    return 1;
}
static inline int medieval_registry_add(JsonDocument *doc, JsonValue *values, const char *name,
                                        JsonValue *value, PatchError *e) {
    JsonValue *entry = json_new(doc, JSON_OBJECT, e);
    if (!entry || !value)
        return 0;
    return json_set(doc, entry, "name", json_text(doc, name, e), e) &&
           json_set(doc, entry, "kind", json_text(doc, value->type == JSON_NUMBER ? "DWord" : "String", e),
                    e) &&
           json_set(doc, entry, "value", value, e) && json_append(values, entry, e);
}
static inline int medieval_registry_installed(JsonDocument *doc, const MedievalIdentity *identity,
                                              const MedievalReceipt *receipt,
                                              const PatchRegistrySnapshot *before, const char *edition,
                                              JsonValue **out, PatchError *e) {
    JsonValue *removed = NULL, *values;
    char *command = NULL, *display = NULL, *suffix = NULL;
    void *memory = NULL;
    size_t n;
    PatchContext local = {0};
    int ok = 0;
    *out = NULL;
    local.allocator = doc->context.allocator;
    local.memory_limit = doc->context.memory_limit;
    if (strcmp(edition, "GOG") && strcmp(edition, "Steam") && strcmp(edition, "Game copy"))
        return json_invalid(e, "Unknown edition label.");
    if (!medieval_registry_ownership(identity, before, receipt->installation_id, e) ||
        !patch_registry_removed(&identity->registry, doc, before, &removed, e))
        goto done;
    values = json_clone(doc, json_get(removed, "values"), e);
    if (!values)
        goto done;
    if (!medieval_uninstall_command(&local, identity, 0, &command, e))
        goto done;
    n = identity->target_length + strlen(edition) + 64;
    if (!patch_alloc(&local, n, 1, &memory, e))
        goto done;
    display = (char *)memory;
    snprintf(display, n, "Unofficial Medieval Patch [%.8s] (%s - %s)", identity->registration_key + 24,
             edition, identity->target);
#define MR_TEXT(k, s)                                                                                        \
    do {                                                                                                     \
        if (!medieval_registry_add(doc, values, (k), json_text(doc, (s), e), e))                             \
            goto done;                                                                                       \
    } while (0)
    MR_TEXT("DisplayName", display);
    MR_TEXT("DisplayVersion", receipt->installer_version);
    MR_TEXT("Publisher", "Louie Woolger");
    MR_TEXT("InstallLocation", identity->target);
    n = strlen(command);
    if (!patch_alloc(&local, n + 4, 1, &memory, e))
        goto done;
    suffix = (char *)memory;
    memcpy(suffix, command, n);
    memcpy(suffix + n, ",0", 3);
    MR_TEXT("DisplayIcon", suffix);
    MR_TEXT("UninstallString", command);
    memcpy(suffix + n, " /S", 4);
    MR_TEXT("QuietUninstallString", suffix);
    if (!medieval_registry_add(doc, values, "NoModify", json_number(doc, 1, e), e) ||
        !medieval_registry_add(doc, values, "NoRepair", json_number(doc, 1, e), e))
        goto done;
    MR_TEXT("URLInfoAbout", "https://github.com/LouieWoolger/medieval-total-war-collection-unofficial-patch");
    MR_TEXT("ProductId", MEDIEVAL_PRODUCT_ID);
    MR_TEXT("InstallationId", receipt->installation_id);
    MR_TEXT("OwnerSid", identity->owner_sid);
#undef MR_TEXT
    if (!patch_registry_sort(&identity->registry, values, e))
        goto done;
    ok = patch_registry_object(doc, 1, before->subkeys, values, out, e);
done:
    patch_context_close(&local);
    return ok;
}
/* Enumeration never duplicates HKLM view aliases on native 32-bit Windows. */
static inline int medieval_legacy_registrations(JsonDocument *doc, const MedievalIdentity *identity,
                                                JsonValue **out, PatchError *e) {
    unsigned h, v, n;
    JsonValue *found = json_new(doc, JSON_ARRAY, e), *before, *after, *entry;
    PatchRegistrySnapshot snapshot;
    int owned;
    *out = NULL;
    if (!found)
        return 0;
    for (h = 0; h < 2; ++h)
        for (v = 0; v < patch_registry_view_count(&identity->registry, h); ++v)
            for (n = 0; n < 2; ++n) {
                const char *hive = h ? "LocalMachine" : "CurrentUser",
                           *view = v ? "Registry64" : "Registry32", *name = patch_legacy_registry_names[n];
                if (!patch_registry_snapshot(&identity->registry, doc, hive, view, name, &before, e) ||
                    !patch_registry_validate(&identity->registry, before, &snapshot, e) ||
                    !medieval_legacy_owned(identity, &snapshot, &owned, e))
                    return 0;
                if (!owned)
                    continue;
                if (!patch_registry_removed(&identity->registry, doc, &snapshot, &after, e))
                    return 0;
                entry = json_new(doc, JSON_OBJECT, e);
                if (!entry || !json_set(doc, entry, "hive", json_text(doc, hive, e), e) ||
                    !json_set(doc, entry, "view", json_text(doc, view, e), e) ||
                    !json_set(doc, entry, "name", json_text(doc, name, e), e) ||
                    !json_set(doc, entry, "before", before, e) || !json_set(doc, entry, "after", after, e) ||
                    !json_append(found, entry, e))
                    return 0;
            }
    *out = found;
    return 1;
}
typedef struct {
    const char *file, *source_relative;
    PatchFileRecord record;
} MedievalArchiveRecord;
typedef struct {
    const JsonValue *json;
    const char *reason, *created_utc;
    size_t count;
    MedievalArchiveRecord files[48];
} MedievalArchive;
static inline int medieval_archive_validate(JsonDocument *doc, const JsonValue *value,
                                            const MedievalIdentity *identity, MedievalArchive *out,
                                            PatchError *e) {
    MedievalArchive archive = {0};
    const JsonValue *files, *v;
    const char *schema, *target, *hash;
    size_t i = 0, k, index;
    int64_t length;
    PatchContext local = {0};
    wchar_t *path = NULL;
    int ok = 0;
    memset(out, 0, sizeof(*out));
    schema = medieval_string(value, "schema", 0, e);
    target = medieval_string(value, "target", 0, e);
    if (!schema || !target || strcmp(schema, "unofficial-medieval-patch-recovery-v1") ||
        strcmp(target, identity->target) || !medieval_integrity_valid(doc, value, e))
        return json_invalid(e, "Invalid recovery archive binding.");
    archive.reason = medieval_string(value, "reason", 0, e);
    archive.created_utc = medieval_string(value, "created_utc", 0, e);
    files = json_get(value, "files");
    if (!archive.reason || !archive.created_utc || !files || files->type != JSON_ARRAY || files->count > 48)
        return json_invalid(e, "Invalid recovery archive records.");
    for (v = files->child; v; v = v->next, ++i) {
        MedievalArchiveRecord *r = &archive.files[i];
        r->file = medieval_string(v, "file", 0, e);
        r->source_relative = medieval_string(v, "source_relative", 0, e);
        hash = medieval_string(v, "sha256", 0, e);
        if (!r->file || !r->source_relative || !hash || !medieval_integer(v, "length", 0, &length, e))
            goto done;
        if (!*r->file || strpbrk(r->file, "\\/:") || !strcmp(r->file, "manifest.json") ||
            !strcmp(r->file, "README.txt") || !medieval_hash_valid(hash) || length < 0 ||
            !medieval_path_role(r->source_relative, &index))
            goto bad;
        for (k = 0; k < i; ++k)
            if (!strcmp(archive.files[k].file, r->file))
                goto bad;
        if (!patch_path_join(&local, identity->guard->canonical, r->file, &path, e))
            goto done;
        patch_context_free(&local, path);
        path = NULL;
        r->record.exists = 1;
        r->record.length = (uint64_t)length;
        memcpy(r->record.sha256, hash, 65);
    }
    archive.json = value;
    archive.count = files->count;
    *out = archive;
    ok = 1;
    goto done;
bad:
    json_invalid(e, "Unsafe recovery archive record.");
done:
    patch_context_close(&local);
    return ok;
}
#endif
