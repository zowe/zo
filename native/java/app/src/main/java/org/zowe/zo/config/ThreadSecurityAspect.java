package org.zowe.zo.config;

import lombok.AllArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.aspectj.lang.ProceedingJoinPoint;
import org.aspectj.lang.annotation.Around;
import org.aspectj.lang.annotation.Aspect;
import org.slf4j.MDC;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;
import org.zowe.zo.zos.security.service.PlatformSecurityService;
import org.zowe.zo.zos.security.thread.PlatformThreadLevelSecurity;

@Aspect
@Component
@AllArgsConstructor
@Slf4j
public class ThreadSecurityAspect {
    public static final String MDC_USER = "user";


    private final PlatformThreadLevelSecurity platformThreadLevelSecurity;
    private final PlatformSecurityService platformSecurityService;

    @Around("execution(* org.zowe.zo.ffm..*(..))")
    public Object wrapInSecurityContext(ProceedingJoinPoint joinPoint) throws Throwable {
        log.debug("Entering {} as user {}", joinPoint, platformSecurityService.getCurrentThreadUserId());
        try {
            return platformThreadLevelSecurity.wrapCallableInEnvironmentForAuthenticatedUser(() -> {
                String previousUser = MDC.get(MDC_USER);
                MDC.put(MDC_USER, platformSecurityService.getCurrentThreadUserId());
                try {
                    log.debug("Executing {} as user {}", joinPoint, MDC.get(MDC_USER));
                    return joinPoint.proceed();
                } catch (Throwable e) {
                    throw new RuntimeException(e);
                } finally {
                    if (previousUser == null) MDC.remove(MDC_USER);
                    else MDC.put(MDC_USER, previousUser);
                }
            }).call();
        } finally {
            log.debug("Exiting {} as user {}", joinPoint, platformSecurityService.getCurrentThreadUserId());
        }
    }
}
