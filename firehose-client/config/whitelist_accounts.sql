-- public.whitelisted_accounts definition

CREATE TABLE public.whitelisted_accounts (
	did varchar NOT NULL,
	CONSTRAINT whitelisted_accounts_pk PRIMARY KEY (did)
);
