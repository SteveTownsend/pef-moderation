-- public.ignored_accounts definition

CREATE TABLE public.ignored_accounts (
	did varchar NOT NULL,
	CONSTRAINT ignored_accounts_pk PRIMARY KEY (did)
);
