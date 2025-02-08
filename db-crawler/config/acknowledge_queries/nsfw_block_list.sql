select
	"subjectDid",
	"subjectUri",
	"createdBy",
	"comment"
from
	public.moderation_event
where
	action = 'tools.ozone.moderation.defs#modEventReport'
	and
            meta->>'reportType' <> 'com.atproto.moderation.defs#reasonAppeal'
	and ("comment" like '%#nsfw%'
		or "comment" like '%#kink%'
		or
"comment" like '#porn'
		or
"comment" like '#nsfw'
		or
"comment" like 'onlyfans.com/'
		or
"comment" like 'loyalfans.com/'
		or
"comment" like 'justfor.fans/'
		or
"comment" like 'allmylinks.com/'
		or
"comment" like 'cammodels.com/'
		or
"comment" like 'fans.ly/'
		or
"comment" like 'top4fans.com/')
