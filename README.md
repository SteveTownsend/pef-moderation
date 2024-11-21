# ozone-bsky
Ozone moderation tools

Supports Bluesky [NAFO custom labeler](nafo-moderation.bsky.social) on domain [nafo-moderation.org](https://ozone.nafo-moderation.org/xrpc/_health?version=0.1.1)
[Installation instructions[(https://github.com/bluesky-social/ozone/blob/main/HOSTING.md)
Suggestions welcome via any of the following:
[Issues tab on this repository](https://github.com/SteveTownsend/ozone-bsky/issues)
[email to service admin](mailto:admin@nafo-moderation.org)
DM or public post to [service account](nafo-moderation.bsky.social)

# Installation Notes
Server is a VPS hosted by Digital Ocean, to the specs suggested, with backups at a small extra cost.
Four domains: nafo-moderation.org/com/net/info set up at Squarespace. They are just the registrar, all DNS setup is done in the Digital Ocean web UI. 
Web server Installation via the Console on the Digital Ocean "Droplet", which is what they call a VPS.
Reports arrive once it's all working properly, and can be managed using a serviceable, but not perfect, [web UI](https://github.com/bluesky-social/ozone/blob/main/docs/userguide.md).
Service account is nafo-moderation.bsky.social.
The endpoint targeted by the reporting API is ozone.moderation.org. I had to add a CNAME record to make that work by redirecting it to nafo-moderation.org. I got confused with domain naming during installation.
Set up nafo-moderation.org is a supported domain in [Proton email](https://proton.me/mail) to support appeals and other stuff. Possibly for other users to help out down the line, too.

# Moderation Policy
The target community is **NAFO and allies**. Broadly, this covers anybody involved in the fight for democracy against authoritarianism rising. State-sponsored disinformation is a cancer. Viral social media is one of its primary vehicles to metastasize.
Content moderation is not limited to support of Ukraine. The scope of the service aligns with [NAFO Forum](https://nafoforum.org/) rather than with [Official NAFO](https://nafo-ofan.org/), which is exclusively focused on support for Ukraine.
All moderation requires human review before a label is applied to content. In the future, conservative automated labeling may be implemented.
This service labels asymmetrically:
- Offending accounts outside the community of NAFO and allies are usually labeled at the account level so abuse is highlighted universally. Experience on X shows that one-time abusers are typically repeat offenders. Post-by-post moderation does not scale.
- For offenders within the community, granular content labelinng is preferred. Account-level labeling is reserved for the worst offenders and requires a two-thirds super-majority of the team to approve. The moderation service must not become a disruptor.
This is to help with managing the  load as the platfrom grows, and to a
There is no plan to act as a verifier of friendly accounts.
Send moderation appeals and other inquiries to [here](mailto:admin@nafo-moderation.org). Appeals of **Label** actions that are not justified in the service's immutable history will be automatically approved. Denial appeals will be supported by provision of relevant moderation history, redacted to remove private information for the protection of moderators.
Target SLA for report moderation is 24 hours. As team grows, the goal is to do better. This may be revised based on real-world constraints and experience.

# Reporting Guidelines
For people who are familiar with social media reporting guidelines, the rules for this service are different. The goal is to disarm accounts posting viral disinformation and other content violations as quickly and broadly as possible.
Typically a single report is sufficient to be dispositive. No more mass reporting or arduous parsing the Terms of Service.
Repeat reports of labeled content add no value. Please ensure you are running with the labels against which you are reporting to avoid duplicate effort

# Moderator Guidelines
All moderators agree to the following:
- moderators accept the risks inherent in performing this work
- Foundational are the [Bluesky Community Guidelines](https://bsky.social/about/support/community-guidelines)
- Moderation philosophy [seminal post](https://bsky.app/profile/nafo-moderation.bsky.social/post/3laz2efafo22x)
- Personal bias, as distinct from this service's inherent editorial bias, must be left at the door. Emotive issues must be handled impartially. Be conservative. Solicit team discussion in controversial or grey areas. When team size reaches six, a two-thirds supermajority is the tiebreaker.
- Must be a proactive volunteer. Solicitation of candidates is prohibited. New moderators attest that they were not solicited to participate.
- Must speak English sufficiently fluent to participate in team discussions
- Abuse of the moderation tools to bully users inside or outside the community or pursue personal grievances is prohibited.
- Reportable behaviour that would result in a content label on any social media or other communication channel is prohibited.
- Provided it is not egregiously in violation of any service label, an amnesty for prior content is granted at the time access is granted to new moderators.
- Late detection of reportable behaviour can be remediated by removal insofar as it is possible, and a team-public commitment to the team to not repeat.
- Important revisions to moderation policy will be published on the service account.
- [Element](https://element.io/) chatroom is used for all subject-relevant moderation team discussions. A commitment to engage there is required, as team votes may be required to tie-break.
- It is permitted to discuss ground rules with candidates before access is granted.
- Sharing outside the moderation team of internal discussions and communications in any form is prohibited. This includes any disruptive behaviour, such as rumour-mongering and screengrabs.
- once team size reaches 10, a new service admin may be nominated by a unanimous vote of the team.
- No rubber stamping of reports. All reported content must be manually reviewed, and a comment affirming the reasons include on all **Label** actions.
- respectful evangelism of the service is encouraged. Disruptive harassment of potential users is prohibited.
- intentional or inadvertent public identification of any moderator other than yourself is prohibited.
- this is unpaid volunteer work. Acceptance or solicitation of any consideration is prohibited.
- no fixed time commitment is required. Moderators may opt out of the team at any time on request to admin. Inactive moderators may be asked whether they wish to remain involved, and removed if not.
- There is zero tolerance for prohibited behaviour deemed by [current service admin](https://bsky.app/profile/stevetownsend0.bsky.social) to be intentional.
New moderators will be provided access to the web UI on written agreement to these guidelines.
Registration at [NAFO Forum](https://nafoforum.org/) to track ongoing efforts to fight disinformation is strongly suggested but not required.

# Moderator Safety
Moderators need a moderation account on Bluesky separate from their personal account to avoid conflicts of interest and harassment on the platform.
Sharing of your personal identification information is not a requirement.
Public acknowledgement by a moderator that they are active on this service is at moderator's sole option. Consider the risks carefully before going public.
Moderation policy discussions should not be held on your public TL or any other public medium. Service admin account on Bluesky is the sole exception.
Admin has amnesty for prior violations of this on his personal account prior to the publication of this document.
A record of active community participation and reliable reporting safeguard before access is provided helps protect against infiltrators. Moderators assume the risk of infiltration.
Once team grows to 6 members, approval by a two-thirds supermajority is required to onboard a new moderator.
Moderation decisions are recorded in the system as public domain information intermingled with moderator identification. When public domain information is published e.g. during an Appeal, all private information must be redacted. Redaction includes obfuscation.

# Moderation Workflow
Currently simple: reports arrive in the Ozone queue and are actioned ad hoc via 
As team grows it is likely this will become:
- initial review of queued report
- either resolve quickly in queue, or **Tag** report for action and **Escalate**

# Future plans
Automation:
- query-based for historic abusive content
- real-time
Running costs are currently covered by startup admin (this poster). If costs increase significantly it may be necessary to find contributions. Costs and funding are public domain information available on request from [admin]().
Metrics:
- SQL server reports are needed to monitor team 
