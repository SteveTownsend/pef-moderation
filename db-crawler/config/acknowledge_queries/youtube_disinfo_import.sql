select
	"subjectDid",
	"subjectUri",
	"createdBy",
	"comment"
from
	public.moderation_event
from
	moderation_event
where
	action = 'tools.ozone.moderation.defs#modEventReport'
	and meta->>'reportType' <> 'com.atproto.moderation.defs#reasonAppeal'
	and "comment" like '%" by %'