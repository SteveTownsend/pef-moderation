-- public.blacklisted_accounts definition

CREATE TABLE public.blacklisted_accounts (
	did varchar NOT NULL,
	CONSTRAINT blacklisted_accounts_pk PRIMARY KEY (did)
);
